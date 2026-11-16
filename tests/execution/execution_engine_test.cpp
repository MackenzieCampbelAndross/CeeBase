#include <gtest/gtest.h>

#include <filesystem>

#include "ceebase/catalog/catalog.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace ceebase {
namespace {

class ExecutionEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_exec_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "exec.nxdb";
        std::filesystem::remove(db_path_);

        dm_ = std::make_unique<DiskManager>();
        (void)dm_->CreateDatabase(db_path_);
        bpm_ = std::make_unique<BufferPoolManager>(10, dm_.get());
        catalog_ = std::make_unique<Catalog>(bpm_.get());
        engine_ = std::make_unique<ExecutionEngine>(bpm_.get(), catalog_.get());
    }

    void TearDown() override {
        engine_.reset();
        catalog_.reset();
        bpm_.reset();
        dm_.reset();
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
    std::unique_ptr<DiskManager> dm_;
    std::unique_ptr<BufferPoolManager> bpm_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<ExecutionEngine> engine_;
};

TEST_F(ExecutionEngineTest, EndToEndCrudWorkflow) {
    // 1. CREATE TABLE
    auto res1 = engine_->Execute("CREATE TABLE employees (id INTEGER PRIMARY KEY, name TEXT, salary DOUBLE);");
    ASSERT_TRUE(res1.success);

    // 2. INSERT multiple rows
    auto res2 = engine_->Execute("INSERT INTO employees VALUES (1, 'Alice', 95000.0), (2, 'Bob', 80000.0), (3, 'Charlie', 110000.0);");
    ASSERT_TRUE(res2.success);
    EXPECT_EQ(res2.rows_affected, 3);

    // 3. SELECT * FROM employees
    auto res3 = engine_->Execute("SELECT * FROM employees;");
    ASSERT_TRUE(res3.success);
    EXPECT_EQ(res3.rows.size(), 3);

    // 4. SELECT with WHERE and ORDER BY
    auto res4 = engine_->Execute("SELECT name, salary FROM employees WHERE salary >= 90000.0 ORDER BY salary DESC;");
    ASSERT_TRUE(res4.success);
    ASSERT_EQ(res4.rows.size(), 2);
    EXPECT_EQ(res4.rows[0][0], "Charlie");
    EXPECT_EQ(res4.rows[1][0], "Alice");

    // 5. UPDATE
    auto res5 = engine_->Execute("UPDATE employees SET salary = 120000.0 WHERE id = 1;");
    ASSERT_TRUE(res5.success);
    EXPECT_EQ(res5.rows_affected, 1);

    // 6. DELETE
    auto res6 = engine_->Execute("DELETE FROM employees WHERE id = 2;");
    ASSERT_TRUE(res6.success);
    EXPECT_EQ(res6.rows_affected, 1);

    // Verify remaining count
    auto res7 = engine_->Execute("SELECT * FROM employees;");
    ASSERT_TRUE(res7.success);
    EXPECT_EQ(res7.rows.size(), 2);
}

TEST_F(ExecutionEngineTest, AggregationAndGroupBy) {
    ASSERT_TRUE(engine_->Execute("CREATE TABLE sales (id INTEGER, rep TEXT, amount DOUBLE);").success);
    ASSERT_TRUE(engine_->Execute("INSERT INTO sales VALUES "
                                 "(1, 'Alice', 100.0), "
                                 "(2, 'Bob', 200.0), "
                                 "(3, 'Alice', 300.0), "
                                 "(4, 'Bob', 150.0);").success);

    // GROUP BY rep with COUNT and SUM
    auto res = engine_->Execute("SELECT rep, COUNT(*), SUM(amount) FROM sales GROUP BY rep;");
    ASSERT_TRUE(res.success);
    EXPECT_EQ(res.rows.size(), 2);
}

TEST_F(ExecutionEngineTest, OptimizerIndexSelectionAndExplain) {
    ASSERT_TRUE(engine_->Execute("CREATE TABLE accounts (id INTEGER PRIMARY KEY, name TEXT, balance DOUBLE);").success);
    ASSERT_TRUE(engine_->Execute("INSERT INTO accounts VALUES (10, 'Corporate', 50000.0), (20, 'Retail', 1200.0);").success);

    // EXPLAIN SELECT * FROM accounts WHERE id = 10;
    auto explain_res = engine_->Execute("EXPLAIN SELECT * FROM accounts WHERE id = 10;");
    ASSERT_TRUE(explain_res.success);
    EXPECT_NE(explain_res.explain_output.find("IndexScan"), std::string::npos);
    EXPECT_NE(explain_res.explain_output.find("Index Used:           YES"), std::string::npos);

    // Execute optimized query directly
    auto query_res = engine_->Execute("SELECT * FROM accounts WHERE id = 10;");
    ASSERT_TRUE(query_res.success);
    ASSERT_EQ(query_res.rows.size(), 1);
    EXPECT_EQ(query_res.rows[0][1], "Corporate");
}

} // namespace
} // namespace ceebase
