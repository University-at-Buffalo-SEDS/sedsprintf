include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
FetchContent_MakeAvailable(googletest)

add_executable(run_tests
    test_tests.cpp
)

target_link_libraries(run_tests PRIVATE
    sedsprintf
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(run_tests)
