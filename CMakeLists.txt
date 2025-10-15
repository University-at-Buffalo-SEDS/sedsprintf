cmake_minimum_required(VERSION 3.16)
project(sedsprintf VERSION 1.0 LANGUAGES C CXX)

cmake_policy(SET CMP0135 NEW)

# Standards
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# OFF by default — tests are *opt-in*
option(BUILD_TESTING "Enable testing" OFF)

add_subdirectory(src)

# Only wire up tests if explicitly requested
if(BUILD_TESTING)
    include(CTest)               # defines BUILD_TESTING
    if(BUILD_TESTING)            # let users still override -DBUILD_TESTING=OFF
        add_subdirectory(tests)
    endif()
endif()
