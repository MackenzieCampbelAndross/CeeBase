add_executable(nexus_tests
    common/status_test.cpp
    common/types_test.cpp
    common/byte_utils_test.cpp
)

target_link_libraries(nexus_tests PRIVATE
    nexus_common
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(nexus_tests)
