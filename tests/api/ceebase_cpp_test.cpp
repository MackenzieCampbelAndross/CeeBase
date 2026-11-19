#include "ceebase/ceebase.hpp"

#include <gtest/gtest.h>
#include <filesystem>

namespace {

class CeebaseCppTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_cpp_api_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        db_path_ = (test_dir_ / "cpp_test.cbdb").string();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::string db_path_;
};

TEST_F(CeebaseCppTest, FullCrudAndTypedBindingWorkflow) {
    ceebase::Connection conn(db_path_);
    EXPECT_EQ(ceebase::Connection::Version(), "1.0.0");

    conn.Execute("CREATE TABLE accounts (id INTEGER PRIMARY KEY, holder TEXT, balance DOUBLE);");
    conn.Execute("INSERT INTO accounts VALUES (1, 'Alice', 1000.50);");
    EXPECT_EQ(conn.Changes(), 1);

    // Prepared statement with parameter binding
    auto stmt = conn.Prepare("SELECT id, holder, balance FROM accounts WHERE id = ?;");
    stmt.Bind(1, 1);
    ASSERT_TRUE(stmt.Step());
    EXPECT_EQ(stmt.GetInt(0), 1);
    EXPECT_EQ(stmt.GetString(1), "Alice");
    EXPECT_DOUBLE_EQ(stmt.GetDouble(2), 1000.50);
    EXPECT_FALSE(stmt.IsNull(0));
    EXPECT_FALSE(stmt.Step()); // No more rows

    // Transaction commit
    conn.Begin();
    conn.Execute("INSERT INTO accounts VALUES (2, 'Bob', 500.00);");
    conn.Commit();

    // Verify Bob exists
    stmt.Reset();
    stmt.Bind(1, 2);
    ASSERT_TRUE(stmt.Step());
    EXPECT_EQ(stmt.GetString(1), "Bob");
    stmt.Reset();

    // Rollback test
    conn.Begin();
    conn.Execute("INSERT INTO accounts VALUES (3, 'Charlie', 200.00);");
    conn.Rollback();

    stmt.Bind(1, 3);
    EXPECT_FALSE(stmt.Step()); // Rolled back!
}

} // namespace
