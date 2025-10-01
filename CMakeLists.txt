cmake_minimum_required(VERSION 3.16)
cmake_policy(SET CMP0135 NEW)
project(sedsprintf VERSION 1.0 LANGUAGES C CXX)

# Standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_subdirectory(src)

enable_testing()

if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()