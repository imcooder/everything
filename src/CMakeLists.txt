set(EVERYTHING_CORE_SOURCES
    app/main.cpp
    core/ntfs_types.cpp
    io/io_service.cpp
    index/bump_string_pool.cpp
    index/index_store.cpp
    index/query_matcher.cpp
    index/query_parser.cpp
    index/utf8_convert.cpp
    ntfs/ntfs_volume_handle.cpp
    ntfs/usn_enumerator.cpp
    ntfs/usn_journal_monitor.cpp
    volume/volume.cpp
    volume/volume_manager.cpp
)

add_executable(Everything.Core ${EVERYTHING_CORE_SOURCES})

target_compile_definitions(Everything.Core PRIVATE
    EVERYTHING_VERSION="${EVERYTHING_VERSION}"
)

target_include_directories(Everything.Core PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(Everything.Core PRIVATE
    Boost::system
    Boost::thread
)

if(WIN32)
    target_link_libraries(Everything.Core PRIVATE
        kernel32
    )
endif()
