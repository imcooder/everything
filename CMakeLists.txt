cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED EVERYTHING_VERSION)
    set(EVERYTHING_VERSION "0.0.0")
endif()

project(Everything VERSION ${EVERYTHING_VERSION} LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4 /permissive-)
endif()

find_package(Boost REQUIRED COMPONENTS system thread)

option(EVERYTHING_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(src)

if(EVERYTHING_BUILD_TESTS)
    enable_testing()
    find_package(GTest CONFIG REQUIRED)
    add_subdirectory(tests)
endif()