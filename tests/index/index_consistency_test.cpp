#include <gtest/gtest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "nexus/catalog/catalog.hpp"
#include "nexus/concurrency/transaction_manager.hpp"
#include "nexus/execution/execution_engine.hpp"
#include "nexus/recovery/log_manager.hpp"
#include "nexus/storage/disk_manager.hpp"

namespace nexus {
namespace {

class IndexConsistencyTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "nexus_index_consistency_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "test_idx.nxdb";
        wal_path_ = test_dir_ / "test_idx.wal";
        std::filesystem::remove(db_path_);
        std::filesystem::remove(wal_path_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
    std::filesystem::path wal_path_;
};

TEST_F(IndexConsistencyTest, InsertAndUniquePrimaryKeyConstraint) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);
    LogManager log_mgr(wal_path_);
    bpm.SetLogManager(&log_mgr);
    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE users (id INTEGER PRIMARY KEY, age INTEGER);").success);

    // Initial insert succeeds
    auto res1 = engine.Execute("INSERT INTO users VALUES (101, 30);");
    EXPECT_TRUE(res1.success);
    EXPECT_EQ(res1.rows_affected, 1);

    // Duplicate primary key insert must fail
    auto res2 = engine.Execute("INSERT INTO users VALUES (101, 35);");
    EXPECT_FALSE(res2.success);
    EXPECT_NE(res2.error_message.find("Duplicate primary key"), std::string::npos);

    // Only 1 row must exist
    auto select_res = engine.Execute("SELECT * FROM users;");
    ASSERT_TRUE(select_res.success);
    EXPECT_EQ(select_res.rows.size(), 1);
    EXPECT_EQ(select_res.rows[0][0], "101");
    EXPECT_EQ(select_res.rows[0][1], "30");
}

TEST_F(IndexConsistencyTest, UpdateIndexedKeyMaintainsIndexSync) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);
    LogManager log_mgr(wal_path_);
    bpm.SetLogManager(&log_mgr);
    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE items (id INTEGER PRIMARY KEY, score INTEGER);").success);
    ASSERT_TRUE(engine.Execute("CREATE INDEX idx_score ON items (score);").success);

    ASSERT_TRUE(engine.Execute("INSERT INTO items VALUES (1, 100);").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO items VALUES (2, 200);").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO items VALUES (3, 300);").success);

    // Update score of item 1 from 100 to 999
    auto update_res = engine.Execute("UPDATE items SET score = 999 WHERE id = 1;");
    ASSERT_TRUE(update_res.success);
    EXPECT_EQ(update_res.rows_affected, 1);

    // Check index directly
    auto* score_idx = catalog.GetIndex("idx_score");
    ASSERT_NE(score_idx, nullptr);
    RID rid;
    EXPECT_FALSE(score_idx->index->GetValue(100, &rid));
    EXPECT_TRUE(score_idx->index->GetValue(999, &rid));

    // Check query using index
    auto sel_res = engine.Execute("SELECT * FROM items WHERE score = 999;");
    ASSERT_TRUE(sel_res.success);
    ASSERT_EQ(sel_res.rows.size(), 1);
    EXPECT_EQ(sel_res.rows[0][0], "1");
    EXPECT_EQ(sel_res.rows[0][1], "999");
}

TEST_F(IndexConsistencyTest, DeleteRowRemovesFromIndex) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);
    LogManager log_mgr(wal_path_);
    bpm.SetLogManager(&log_mgr);
    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE records (id INTEGER PRIMARY KEY, val INTEGER);").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO records VALUES (10, 100);").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO records VALUES (20, 200);").success);

    auto* pk_idx = catalog.GetIndex("records_pk_idx");
    ASSERT_NE(pk_idx, nullptr);
    RID rid;
    EXPECT_TRUE(pk_idx->index->GetValue(10, &rid));

    // Delete record 10
    auto del_res = engine.Execute("DELETE FROM records WHERE id = 10;");
    ASSERT_TRUE(del_res.success);
    EXPECT_EQ(del_res.rows_affected, 1);

    // Index lookup for 10 must return false
    EXPECT_FALSE(pk_idx->index->GetValue(10, &rid));
    // Index lookup for 20 must remain intact
    EXPECT_TRUE(pk_idx->index->GetValue(20, &rid));

    // Reinserting id 10 should now succeed
    auto reinsert = engine.Execute("INSERT INTO records VALUES (10, 555);");
    EXPECT_TRUE(reinsert.success);
    EXPECT_TRUE(pk_idx->index->GetValue(10, &rid));
}

TEST_F(IndexConsistencyTest, RollbackRestoresIndexEntries) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);
    LogManager log_mgr(wal_path_);
    bpm.SetLogManager(&log_mgr);
    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, amount INTEGER);").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO ledger VALUES (1, 100);").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO ledger VALUES (2, 200);").success);

    auto* pk_idx = catalog.GetIndex("ledger_pk_idx");
    ASSERT_NE(pk_idx, nullptr);

    // Begin transaction and mutate
    ASSERT_TRUE(engine.Execute("BEGIN;").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO ledger VALUES (3, 300);").success);
    ASSERT_TRUE(engine.Execute("DELETE FROM ledger WHERE id = 1;").success);
    ASSERT_TRUE(engine.Execute("UPDATE ledger SET amount = 999 WHERE id = 2;").success);

    // Rollback!
    ASSERT_TRUE(engine.Execute("ROLLBACK;").success);

    // Verification:
    // Row 3 must be absent from table and index
    RID rid;
    EXPECT_FALSE(pk_idx->index->GetValue(3, &rid));
    // Row 1 must be present in table and index
    EXPECT_TRUE(pk_idx->index->GetValue(1, &rid));
    // Row 2 must have original amount 200
    EXPECT_TRUE(pk_idx->index->GetValue(2, &rid));

    auto sel = engine.Execute("SELECT * FROM ledger ORDER BY id ASC;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), 2);
    EXPECT_EQ(sel.rows[0][0], "1");
    EXPECT_EQ(sel.rows[0][1], "100");
    EXPECT_EQ(sel.rows[1][0], "2");
    EXPECT_EQ(sel.rows[1][1], "200");
}

} // namespace
} // namespace nexus
