#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <random>
#include <string>
#include <vector>

#include "ceebase/catalog/catalog.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/recovery/log_manager.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace ceebase {
namespace {

class TransactionStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_txn_stress_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "stress_txn.nxdb";
        wal_path_ = test_dir_ / "stress_txn.wal";
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

TEST_F(TransactionStressTest, HighVolumeTransactionalCrudWithSmallBufferPool) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    // Very small buffer pool (5 frames) under high write volume forces constant eviction
    LogManager log_mgr(wal_path_);
    BufferPoolManager bpm(5, &dm, &log_mgr);
    Catalog catalog(&bpm);
    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE accounts (id INTEGER PRIMARY KEY, balance INTEGER);").success);

    std::map<int, int> oracle; // Reference ground truth
    std::mt19937 rng(1337);

    // 100 transactions performing mixed operations
    for (int t = 0; t < 100; ++t) {
        ASSERT_TRUE(engine.Execute("BEGIN;").success);

        int op_type = t % 5;
        if (op_type == 0 || op_type == 1 || op_type == 2) {
            // Commit insert
            int id = t + 1;
            int bal = (t + 1) * 10;
            std::string sql = "INSERT INTO accounts VALUES (" + std::to_string(id) + ", " + std::to_string(bal) + ");";
            ASSERT_TRUE(engine.Execute(sql).success);
            ASSERT_TRUE(engine.Execute("COMMIT;").success);
            oracle[id] = bal;
        } else if (op_type == 3) {
            // Rollback insert
            int id = 1000 + t;
            int bal = 9999;
            std::string sql = "INSERT INTO accounts VALUES (" + std::to_string(id) + ", " + std::to_string(bal) + ");";
            ASSERT_TRUE(engine.Execute(sql).success);
            ASSERT_TRUE(engine.Execute("ROLLBACK;").success);
            // Must not be in oracle
        } else {
            // Update an existing key
            if (!oracle.empty()) {
                auto it = oracle.begin();
                int target_id = it->first;
                int new_bal = it->second + 500;
                std::string sql = "UPDATE accounts SET balance = " + std::to_string(new_bal) + " WHERE id = " + std::to_string(target_id) + ";";
                ASSERT_TRUE(engine.Execute(sql).success);
                ASSERT_TRUE(engine.Execute("COMMIT;").success);
                oracle[target_id] = new_bal;
            } else {
                ASSERT_TRUE(engine.Execute("COMMIT;").success);
            }
        }
    }

    // Verify all rows match oracle ground truth
    auto sel = engine.Execute("SELECT * FROM accounts ORDER BY id ASC;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), oracle.size());

    size_t row_idx = 0;
    auto* pk_idx = catalog.GetIndex("accounts_pk_idx");
    ASSERT_NE(pk_idx, nullptr);

    for (const auto& [expected_id, expected_bal] : oracle) {
        EXPECT_EQ(std::stoi(sel.rows[row_idx][0]), expected_id);
        EXPECT_EQ(std::stoi(sel.rows[row_idx][1]), expected_bal);

        // Point lookup through index must find the exact RID
        RID rid;
        EXPECT_TRUE(pk_idx->index->GetValue(expected_id, &rid));
        row_idx++;
    }
}

TEST_F(TransactionStressTest, LargeTuplesSpanningManyPages) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    LogManager log_mgr(wal_path_);
    BufferPoolManager bpm(6, &dm, &log_mgr);
    Catalog catalog(&bpm);
    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE documents (id INTEGER PRIMARY KEY, content TEXT);").success);

    // Insert 100 large tuples (~500 bytes each) which will allocate ~15+ slotted pages
    std::string large_string(450, 'Z');
    for (int i = 1; i <= 100; ++i) {
        std::string sql = "INSERT INTO documents VALUES (" + std::to_string(i) + ", '" + large_string + "');";
        auto res = engine.Execute(sql);
        ASSERT_TRUE(res.success) << res.error_message;
    }

    auto count_res = engine.Execute("SELECT COUNT(*) FROM documents;");
    ASSERT_TRUE(count_res.success);
    ASSERT_EQ(count_res.rows.size(), 1);
    EXPECT_EQ(count_res.rows[0][0], "100");

    // Scan through all tuples to ensure multi-page chain traversal works
    auto sel = engine.Execute("SELECT id FROM documents ORDER BY id ASC;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), 100);
    for (int i = 0; i < 100; ++i) {
        EXPECT_EQ(std::stoi(sel.rows[static_cast<size_t>(i)][0]), i + 1);
    }
}

} // namespace
} // namespace ceebase
