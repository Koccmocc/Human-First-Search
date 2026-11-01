cmake_minimum_required(VERSION 3.30)

# Lexbor is written in C; our crawler remains C++20.
project(HumanFirstSearch VERSION 0.1.0 LANGUAGES C CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "Use an out-of-source build: cmake --preset debug")
endif()

if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(FATAL_ERROR "This project currently supports GCC. Use the provided presets.")
endif()

# Require the selected standard instead of silently falling back to an older one.
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CTest)
include(cmake/HtmlParser.cmake)
if(BUILD_TESTING)
    include(cmake/Testing.cmake)
endif()
add_subdirectory(crawler)
