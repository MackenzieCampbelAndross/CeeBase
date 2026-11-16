#include <gtest/gtest.h>

#include <filesystem>

#include "nexus/catalog/catalog.hpp"
#include "nexus/concurrency/transaction_manager.hpp"
#include "nexus/execution/execution_engine.hpp"
#include "nexus/recovery/log_manager.hpp"
#include "nexus/recovery/recovery_engine.hpp"
#include "nexus/storage/disk_manager.hpp"

namespace nexus {
namespace {

class RecoveryTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "nexus_recovery_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "recovery.nxdb";
        wal_path_ = test_dir_ / "recovery.wal";
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

TEST_F(RecoveryTest, TransactionCommitAndRollback) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);
    LogManager log_mgr(wal_path_);
    TransactionManager txn_mgr(&log_mgr, &bpm);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    ASSERT_TRUE(engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, balance DOUBLE);").success);

    // 1. Committed Transaction
    ASSERT_TRUE(engine.Execute("BEGIN;").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO ledger VALUES (1, 500.0);").success);
    ASSERT_TRUE(engine.Execute("COMMIT;").success);

    auto res1 = engine.Execute("SELECT * FROM ledger;");
    ASSERT_TRUE(res1.success);
    EXPECT_EQ(res1.rows.size(), 1);

    // 2. Aborted Transaction (Rollback)
    ASSERT_TRUE(engine.Execute("BEGIN;").success);
    ASSERT_TRUE(engine.Execute("INSERT INTO ledger VALUES (2, 900.0);").success);
    ASSERT_TRUE(engine.Execute("ROLLBACK;").success);

    // Row 2 must have been undone!
    auto res2 = engine.Execute("SELECT * FROM ledger;");
    ASSERT_TRUE(res2.success);
    EXPECT_EQ(res2.rows.size(), 1);
    EXPECT_EQ(res2.rows[0][0], "1");
}

TEST_F(RecoveryTest, CrashRecoveryUncommittedTransactionRollback) {
    // Phase 1: Simulate crash with an uncommitted transaction
    page_id_t vault_root_page = INVALID_PAGE_ID;
    {
        DiskManager dm;
        ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());
        BufferPoolManager bpm(10, &dm);
        Catalog catalog(&bpm);
        LogManager log_mgr(wal_path_);
        TransactionManager txn_mgr(&log_mgr, &bpm);

        std::vector<Column> cols;
        cols.emplace_back("id", DataType::INTEGER, false);
        cols.emplace_back("val", DataType::DOUBLE, false);
        auto tbl_res = catalog.CreateTable("vault", Schema(std::move(cols)));
        ASSERT_TRUE(tbl_res.ok());
        auto* tbl = *tbl_res;
        vault_root_page = tbl->table_heap->GetFirstPageId();

        // Txn 1: Commits
        auto txn1 = txn_mgr.Begin();
        Tuple t1({Value(1), Value(100.0)}, tbl->schema);
        RID r1;
        ASSERT_TRUE(tbl->table_heap->InsertTuple(t1, &r1).ok());
        auto ins_rec1 = LogRecord::MakeInsert(txn1->GetTxnId(), txn1->GetPrevLSN(), "vault", r1,
                                               std::vector<uint8_t>(t1.GetData(), t1.GetData() + t1.GetLength()));
        (void)log_mgr.AppendRecord(&ins_rec1);
        ASSERT_TRUE(txn_mgr.Commit(txn1.get()).ok());

        // Txn 2: Incomplete (Crashes before commit!)
        auto txn2 = txn_mgr.Begin();
        Tuple t2({Value(2), Value(200.0)}, tbl->schema);
        RID r2;
        ASSERT_TRUE(tbl->table_heap->InsertTuple(t2, &r2).ok());
        auto ins_rec2 = LogRecord::MakeInsert(txn2->GetTxnId(), txn2->GetPrevLSN(), "vault", r2,
                                               std::vector<uint8_t>(t2.GetData(), t2.GetData() + t2.GetLength()));
        (void)log_mgr.AppendRecord(&ins_rec2);
        (void)log_mgr.Flush(); // Log on disk, but txn never committed!

        // Crash occurs here: dm, bpm, log_mgr destructed abruptly
    }

    // Phase 2: Restart and run RecoveryEngine
    {
        DiskManager dm;
        ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
        BufferPoolManager bpm(10, &dm);
        Catalog catalog(&bpm);
        std::vector<Column> cols;
        cols.emplace_back("id", DataType::INTEGER, false);
        cols.emplace_back("val", DataType::DOUBLE, false);
        auto* tbl = catalog.RegisterExistingTable("vault", Schema(std::move(cols)), vault_root_page).value();

        LogManager log_mgr(wal_path_);
        RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

        // Run recovery
        ASSERT_TRUE(recovery.Recover().ok());
        EXPECT_EQ(recovery.GetActiveTxnCount(), 0); // Uncommitted txn2 was undone
        EXPECT_EQ(recovery.GetCommittedTxnCount(), 1); // Txn1 committed

        // Verify row 1 exists, row 2 is gone
        int count = 0;
        for (auto it = tbl->table_heap->Begin(); it != tbl->table_heap->End(); ++it) {
            count++;
            EXPECT_EQ(it->GetValue(tbl->schema, 0).GetInteger(), 1);
        }
        EXPECT_EQ(count, 1);
    }
}

} // namespace
} // namespace nexus
