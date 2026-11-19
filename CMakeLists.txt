cmake_minimum_required(VERSION 3.20)
project(CeeBase VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for language servers
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Strict Compiler Warnings
if(MSVC)
    add_compile_options(/W4 /permissive-)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wno-unused-parameter)
endif()

# Sanitizers option
option(CEEBASE_ENABLE_SANITIZERS "Enable Address and Undefined Behavior Sanitizers" OFF)
if(CEEBASE_ENABLE_SANITIZERS AND NOT MSVC)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()

# Core CeeBase Engine Library
add_library(ceebase_core
    src/common/status.cpp
    src/common/byte_utils.cpp
    src/storage/disk_manager.cpp
    src/buffer/lru_replacer.cpp
    src/buffer/buffer_pool_manager.cpp
    src/record/slotted_page.cpp
    src/record/value.cpp
    src/record/tuple.cpp
    src/record/table_heap.cpp
    src/index/b_plus_tree.cpp
    src/catalog/catalog.cpp
    src/parser/lexer.cpp
    src/parser/ast.cpp
    src/parser/parser.cpp
    src/planner/logical_plan.cpp
    src/planner/planner.cpp
    src/planner/optimizer.cpp
    src/recovery/log_manager.cpp
    src/concurrency/transaction_manager.cpp
    src/recovery/recovery_engine.cpp
    src/execution/execution_engine.cpp
    src/api/ceebase_c.cpp
)

target_include_directories(ceebase_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
set_target_properties(ceebase_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# C ABI Dynamic Shared Library (ceebase.dll / libceebase.so)
add_library(ceebase_shared SHARED
    src/api/ceebase_c.cpp
)
target_link_libraries(ceebase_shared PRIVATE ceebase_core)
target_compile_definitions(ceebase_shared PRIVATE CEEBASE_BUILD_DLL)
set_target_properties(ceebase_shared PROPERTIES OUTPUT_NAME "ceebase")

# Interactive Command Line Interface Executable
add_executable(ceebase
    src/cli/main.cpp
)

target_link_libraries(ceebase PRIVATE
    ceebase_core
)

# Benchmark Suite Executable
add_executable(ceebase_bench
    benchmarks/bench_main.cpp
)

target_link_libraries(ceebase_bench PRIVATE
    ceebase_core
)

# Test Suite Configuration
option(CEEBASE_BUILD_TESTS "Build CeeBase unit and integration test suite" ON)

if(CEEBASE_BUILD_TESTS)
    enable_testing()

    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG v1.14.0
        )
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()

    add_subdirectory(tests)
endif()
