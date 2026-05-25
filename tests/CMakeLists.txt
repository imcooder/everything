add_executable(Everything.Tests
    bump_string_pool_test.cpp
    query_matcher_test.cpp
    query_parser_test.cpp
    index_path_test.cpp
    ${CMAKE_SOURCE_DIR}/src/index/bump_string_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/index/index_store.cpp
    ${CMAKE_SOURCE_DIR}/src/index/query_matcher.cpp
    ${CMAKE_SOURCE_DIR}/src/index/query_parser.cpp
    ${CMAKE_SOURCE_DIR}/src/index/utf8_convert.cpp
)

target_include_directories(Everything.Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(Everything.Tests PRIVATE
    GTest::gtest_main
)

if(WIN32)
    target_link_libraries(Everything.Tests PRIVATE
        kernel32
    )
endif()

include(GoogleTest)
gtest_discover_tests(Everything.Tests DISCOVERY_MODE PRE_TEST)
