add_executable(Everything.Tests
    bump_string_pool_test.cpp
    query_matcher_test.cpp
    ${CMAKE_SOURCE_DIR}/src/index/bump_string_pool.cpp
    ${CMAKE_SOURCE_DIR}/src/index/query_matcher.cpp
)

target_include_directories(Everything.Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(Everything.Tests PRIVATE
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(Everything.Tests DISCOVERY_MODE PRE_TEST)
