cmake_minimum_required(VERSION 3.22)
project(c_example_code C CXX)

set(CMAKE_C_STANDARD 11)

# 1) Define the target first
add_executable(c_example_code
        src/main.c
        src/telemetry.c
)

# 2) Local includes (your own headers)
target_include_directories(c_example_code PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

get_filename_component(SEDSPRINTF_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." REALPATH)
add_subdirectory(
        "${SEDSPRINTF_ROOT}"
        "${CMAKE_CURRENT_BINARY_DIR}/sedsprintf-build"
        EXCLUDE_FROM_ALL
)

# 4) Link + add the libraries public headers
target_link_libraries(c_example_code PRIVATE sedsprintf)
target_include_directories(c_example_code PRIVATE "${SEDSPRINTF_INCLUDE_DIR}")

