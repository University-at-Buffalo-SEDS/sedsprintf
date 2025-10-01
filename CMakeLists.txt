cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0135 NEW)
project(sedsprintf VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

enable_testing()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()