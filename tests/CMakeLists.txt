add_executable(nexus_tests
    common/status_test.cpp
    common/types_test.cpp
    common/byte_utils_test.cpp
    storage/disk_manager_test.cpp
    storage/storage_stress_test.cpp
    buffer/lru_replacer_test.cpp
    buffer/buffer_pool_manager_test.cpp
    record/slotted_page_test.cpp
    record/tuple_test.cpp
    record/table_heap_test.cpp
    index/b_plus_tree_test.cpp
    parser/sql_parser_test.cpp
    execution/execution_engine_test.cpp
    recovery/recovery_test.cpp
)

target_link_libraries(nexus_tests PRIVATE
    nexus_core
    GTest::gtest
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(nexus_tests)
