# Standalone Crash Injection Worker (runs in a separate OS process and terminates abruptly)
add_executable(ceebase_crash_worker
    recovery/crash_worker.cpp
)

target_link_libraries(ceebase_crash_worker PRIVATE
    ceebase_core
)

# Unified GTest Test Runner
add_executable(ceebase_tests
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
    index/index_consistency_test.cpp
    parser/sql_parser_test.cpp
    execution/execution_engine_test.cpp
    recovery/recovery_test.cpp
    recovery/crash_test_harness.cpp
    concurrency/transaction_stress_test.cpp
)

target_link_libraries(ceebase_tests PRIVATE
    ceebase_core
    GTest::gtest
    GTest::gtest_main
)

# Ensure worker binary is built before running tests
add_dependencies(ceebase_tests ceebase_crash_worker)

include(GoogleTest)
gtest_discover_tests(ceebase_tests)
