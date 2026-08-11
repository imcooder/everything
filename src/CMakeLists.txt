# Final shippable binaries (Everything.Core, Everything.UI — not the Diag*/Tests dev tools)
# are copied here on every build so there's one stable place to grab a build from, regardless
# of generator/toolset output layout. Subdirectory is the target platform.
if(WIN32)
    set(EVERYTHING_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output/windows-x64")
else()
    set(EVERYTHING_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/output/${CMAKE_SYSTEM_NAME}")
endif()

function(everything_copy_to_output_dir target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${EVERYTHING_OUTPUT_DIR}/$<CONFIG>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${target_name}>" "${EVERYTHING_OUTPUT_DIR}/$<CONFIG>/"
        COMMENT "Copying ${target_name} to ${EVERYTHING_OUTPUT_DIR}/$<CONFIG>/"
    )
endfunction()

set(EVERYTHING_BACKEND_SOURCES
    core/ntfs_types.cpp
    io/io_service.cpp
    index/bump_string_pool.cpp
    index/index_persistence.cpp
    index/index_store.cpp
    index/query_matcher.cpp
    index/query_parser.cpp
    index/utf8_convert.cpp
    ntfs/mft_reader.cpp
    ntfs/mft_record.cpp
    ntfs/ntfs_volume_handle.cpp
    ntfs/usn_enumerator.cpp
    ntfs/usn_journal_monitor.cpp
    volume/volume.cpp
    volume/volume_manager.cpp
)

# Shared static library: index/volume/ntfs/io layers used by both the console harness
# (Everything.Core) and the WTL UI (Everything.UI). Indexing/USN/search logic must not
# depend on UI code (README §6), so this library never sees WTL/ATL headers.
add_library(Everything.Backend STATIC ${EVERYTHING_BACKEND_SOURCES})

target_include_directories(Everything.Backend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Everything.Backend PUBLIC
    Boost::system
    Boost::thread
)

if(WIN32)
    target_link_libraries(Everything.Backend PUBLIC
        kernel32
    )
endif()

# ---------------------------------------------------------------------------
# Everything.Core — console harness (headless, useful for debugging without a UI).
# ---------------------------------------------------------------------------
add_executable(Everything.Core app/main.cpp)

target_compile_definitions(Everything.Core PRIVATE
    EVERYTHING_VERSION="${EVERYTHING_VERSION}"
)

target_link_libraries(Everything.Core PRIVATE
    Everything.Backend
)

everything_copy_to_output_dir(Everything.Core)

# ---------------------------------------------------------------------------
# Everything.UI — WTL-based Win32 windowed UI (search box + virtual ListView + status bar).
# ---------------------------------------------------------------------------
if(WIN32)
    find_path(WTL_INCLUDE_DIRS "atlapp.h")

    set(EVERYTHING_UI_SOURCES
        app/ui/main_ui.cpp
        app/ui/main_frame.cpp
        app/ui/result_model.cpp
        app/ui/ui_search_sink.cpp
    )

    add_executable(Everything.UI WIN32 ${EVERYTHING_UI_SOURCES})

    target_compile_definitions(Everything.UI PRIVATE
        EVERYTHING_VERSION="${EVERYTHING_VERSION}"
        UNICODE
        _UNICODE
        STRICT
        _WTL_NO_CSTRING
        _WIN32_WINNT=0x0601
        WINVER=0x0601
    )

    target_include_directories(Everything.UI PRIVATE
        ${WTL_INCLUDE_DIRS}
    )

    target_link_libraries(Everything.UI PRIVATE
        Everything.Backend
        comctl32
        shell32
        shlwapi
        ole32
        uxtheme
    )

    set_target_properties(Everything.UI PROPERTIES
        VS_DPI_AWARE "PerMonitor"
    )

    # main() (not WinMain/wWinMain) drives the WTL message loop directly — force the CRT
    # entry thunk that calls main() even though the WIN32 property above selects the
    # Windows (GUI, no console) subsystem. Avoids relying on MSVC's WinMain/wWinMain
    # auto-detection, which is sensitive to how the project is generated.
    if(MSVC)
        target_link_options(Everything.UI PRIVATE "/ENTRY:mainCRTStartup")
    endif()

    everything_copy_to_output_dir(Everything.UI)

    # ---------------------------------------------------------------------------
    # Everything.UI.LiveUpdateTest — in-process integration check that CMainFrame's live
    # ListView actually reacts to WM_APP_SEARCH_* events the same way a real CUiSearchSink
    # would post them (docs/ui-live-search-testcases.md UI7-UI12). See
    # tests/ui_live_update_integration.cpp for exactly what this does and does not prove.
    # Console subsystem (not WIN32) so its printf() PASS/FAIL output is directly capturable.
    # ---------------------------------------------------------------------------
    add_executable(Everything.UI.LiveUpdateTest
        ${CMAKE_SOURCE_DIR}/tests/ui_live_update_integration.cpp
        app/ui/main_frame.cpp
        app/ui/result_model.cpp
        app/ui/ui_search_sink.cpp
    )

    target_compile_definitions(Everything.UI.LiveUpdateTest PRIVATE
        EVERYTHING_VERSION="${EVERYTHING_VERSION}"
        UNICODE
        _UNICODE
        STRICT
        _WTL_NO_CSTRING
        _WIN32_WINNT=0x0601
        WINVER=0x0601
    )

    target_include_directories(Everything.UI.LiveUpdateTest PRIVATE
        ${WTL_INCLUDE_DIRS}
    )

    target_link_libraries(Everything.UI.LiveUpdateTest PRIVATE
        Everything.Backend
        comctl32
        shell32
        shlwapi
        ole32
        uxtheme
    )
endif()

# ---------------------------------------------------------------------------
# Manual real-volume diagnostics — not run by CI (no admin/real-disk access there), used to
# verify load/monitor/search against actual NTFS volumes by hand. See tests/diag_*.cpp.
# ---------------------------------------------------------------------------
add_executable(Everything.DiagSingleVolume ${CMAKE_SOURCE_DIR}/tests/diag_single_volume.cpp)
target_link_libraries(Everything.DiagSingleVolume PRIVATE Everything.Backend)

add_executable(Everything.DiagMonitorTest ${CMAKE_SOURCE_DIR}/tests/diag_monitor_test.cpp)
target_link_libraries(Everything.DiagMonitorTest PRIVATE Everything.Backend)

add_executable(Everything.DiagSearchTest ${CMAKE_SOURCE_DIR}/tests/diag_search_test.cpp)
target_link_libraries(Everything.DiagSearchTest PRIVATE Everything.Backend)

add_executable(Everything.DiagSoakTest ${CMAKE_SOURCE_DIR}/tests/diag_soak_test.cpp)
target_link_libraries(Everything.DiagSoakTest PRIVATE Everything.Backend)
