#include "ceebase/ceebase_c.h"

#include <gtest/gtest.h>
#include <filesystem>
#include <string>

namespace {

class CeebaseCTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_c_api_test";
        std::filesystem::remove_all(test_dir_);
        std::filesystem::create_directories(test_dir_);
        db_path_ = (test_dir_ / "test_api.cbdb").string();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::string db_path_;
};

TEST_F(CeebaseCTest, VersionAndBasicOpenClose) {
    EXPECT_STREQ(ceebase_version(), "1.0.0");

    ceebase_db* db = nullptr;
    int rc = ceebase_open(db_path_.c_str(), &db);
    ASSERT_EQ(rc, CEEBASE_OK);
    ASSERT_NE(db, nullptr);

    rc = ceebase_close(db);
    EXPECT_EQ(rc, CEEBASE_OK);
}

TEST_F(CeebaseCTest, ExecCreateTableInsertAndError) {
    ceebase_db* db = nullptr;
    ASSERT_EQ(ceebase_open(db_path_.c_str(), &db), CEEBASE_OK);

    char* errmsg = nullptr;
    int rc = ceebase_exec(db, "CREATE TABLE users (id INTEGER PRIMARY KEY, name TEXT, balance DOUBLE);", &errmsg);
    EXPECT_EQ(rc, CEEBASE_OK);
    EXPECT_EQ(errmsg, nullptr);

    rc = ceebase_exec(db, "INSERT INTO users VALUES (1, 'Alice', 150.25);", &errmsg);
    EXPECT_EQ(rc, CEEBASE_OK);
    EXPECT_EQ(ceebase_changes(db), 1);

    // Malformed query should report error
    rc = ceebase_exec(db, "INVALID SQL STATEMENT;", &errmsg);
    EXPECT_EQ(rc, CEEBASE_ERROR);
    EXPECT_NE(errmsg, nullptr);
    ceebase_free_errmsg(errmsg);

    ceebase_close(db);
}

TEST_F(CeebaseCTest, PrepareStepAndColumnAccessors) {
    ceebase_db* db = nullptr;
    ASSERT_EQ(ceebase_open(db_path_.c_str(), &db), CEEBASE_OK);

    ASSERT_EQ(ceebase_exec(db, "CREATE TABLE items (id INTEGER PRIMARY KEY, title TEXT, price DOUBLE);", nullptr), CEEBASE_OK);
    ASSERT_EQ(ceebase_exec(db, "INSERT INTO items VALUES (10, 'Book', 19.99);", nullptr), CEEBASE_OK);
    ASSERT_EQ(ceebase_exec(db, "INSERT INTO items VALUES (20, 'Pen', 2.50);", nullptr), CEEBASE_OK);

    ceebase_stmt* stmt = nullptr;
    int rc = ceebase_prepare(db, "SELECT id, title, price FROM items ORDER BY id;", &stmt);
    ASSERT_EQ(rc, CEEBASE_OK);
    ASSERT_NE(stmt, nullptr);

    // Row 1
    rc = ceebase_step(stmt);
    EXPECT_EQ(rc, CEEBASE_ROW);
    EXPECT_EQ(ceebase_column_count(stmt), 3);
    EXPECT_STREQ(ceebase_column_name(stmt, 0), "id");
    EXPECT_STREQ(ceebase_column_name(stmt, 1), "title");
    EXPECT_STREQ(ceebase_column_name(stmt, 2), "price");
    EXPECT_EQ(ceebase_column_int(stmt, 0), 10);
    EXPECT_STREQ(ceebase_column_text(stmt, 1), "Book");
    EXPECT_DOUBLE_EQ(ceebase_column_double(stmt, 2), 19.99);

    // Row 2
    rc = ceebase_step(stmt);
    EXPECT_EQ(rc, CEEBASE_ROW);
    EXPECT_EQ(ceebase_column_int(stmt, 0), 20);
    EXPECT_STREQ(ceebase_column_text(stmt, 1), "Pen");
    EXPECT_DOUBLE_EQ(ceebase_column_double(stmt, 2), 2.50);

    // Done
    rc = ceebase_step(stmt);
    EXPECT_EQ(rc, CEEBASE_DONE);

    ceebase_finalize(stmt);

    // Test Parameter Binding and Statement Reset
    ceebase_stmt* p_stmt = nullptr;
    ASSERT_EQ(ceebase_prepare(db, "SELECT id, title, price FROM items WHERE id = ?;", &p_stmt), CEEBASE_OK);
    ASSERT_EQ(ceebase_bind_int(p_stmt, 1, 10), CEEBASE_OK);

    ASSERT_EQ(ceebase_step(p_stmt), CEEBASE_ROW);
    EXPECT_EQ(ceebase_column_int(p_stmt, 0), 10);
    EXPECT_STREQ(ceebase_column_text(p_stmt, 1), "Book");

    // Reset and bind parameter 20
    ASSERT_EQ(ceebase_reset(p_stmt), CEEBASE_OK);
    ASSERT_EQ(ceebase_bind_int(p_stmt, 1, 20), CEEBASE_OK);

    ASSERT_EQ(ceebase_step(p_stmt), CEEBASE_ROW);
    EXPECT_EQ(ceebase_column_int(p_stmt, 0), 20);
    EXPECT_STREQ(ceebase_column_text(p_stmt, 1), "Pen");

    ceebase_finalize(p_stmt);
    ceebase_close(db);
}

} // namespace
