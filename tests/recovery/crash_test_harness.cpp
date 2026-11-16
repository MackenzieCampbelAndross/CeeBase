#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "ceebase/catalog/catalog.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/recovery/log_manager.hpp"
#include "ceebase/recovery/recovery_engine.hpp"
#include "ceebase/storage/disk_manager.hpp"

#ifdef _WIN32
#include <process.h>
#else
#include <sys/wait.h>
#endif

namespace ceebase {
namespace {

std::filesystem::path FindCrashWorkerExecutable() {
    std::vector<std::filesystem::path> candidates = {
        "tests/ceebase_crash_worker.exe",
        "ceebase_crash_worker.exe",
        "./ceebase_crash_worker.exe",
        "tests/ceebase_crash_worker",
        "ceebase_crash_worker",
        "./ceebase_crash_worker",
        "build/tests/ceebase_crash_worker.exe",
        "build/tests/ceebase_crash_worker"
    };

    for (const auto& p : candidates) {
        if (std::filesystem::exists(p)) {
            return std::filesystem::absolute(p);
        }
    }
    return "";
}

int RunCrashWorker(const std::filesystem::path& worker_exe,
                   const std::filesystem::path& db_path,
                   int scenario) {
#ifdef _WIN32
    std::string scenario_str = std::to_string(scenario);
    intptr_t ret = _spawnl(_P_WAIT, worker_exe.string().c_str(),
                           worker_exe.string().c_str(),
                           db_path.string().c_str(),
                           scenario_str.c_str(),
                           nullptr);
    return static_cast<int>(ret);
#else
    std::string cmd = worker_exe.string() + " " + db_path.string() + " " + std::to_string(scenario);
    int code = std::system(cmd.c_str());
    if (WIFEXITED(code)) {
        return WEXITSTATUS(code);
    }
    return -1;
#endif
}

class CrashRecoveryProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_process_crash_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "crash_test.nxdb";
        wal_path_ = test_dir_ / "crash_test.wal";
        std::filesystem::remove(db_path_);
        std::filesystem::remove(wal_path_);
        worker_exe_ = FindCrashWorkerExecutable();
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
    std::filesystem::path wal_path_;
    std::filesystem::path worker_exe_;
};

// Scenario 1: Crash before WAL persistence (uncommitted and unpersisted)
TEST_F(CrashRecoveryProcessTest, Scenario1_CrashBeforeWalPersistence) {
    ASSERT_FALSE(worker_exe_.empty()) << "ceebase_crash_worker binary not found";
    int exit_code = RunCrashWorker(worker_exe_, db_path_, 1);
    EXPECT_EQ(exit_code, 42);

    // Verify recovery handling
    DiskManager dm;
    ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);

    std::vector<Column> cols;
    cols.emplace_back("id", DataType::INTEGER, false);
    cols.emplace_back("balance", DataType::DOUBLE, false);
    (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");

    LogManager log_mgr(wal_path_);
    RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

    // Run recovery
    auto rec_status = recovery.Recover();
    EXPECT_TRUE(rec_status.ok());

    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    // Table was created, but uncommitted insert was not persisted to WAL
    auto* tbl = catalog.GetTable("ledger");
    if (tbl) {
        auto sel = engine.Execute("SELECT * FROM ledger;");
        EXPECT_TRUE(sel.success);
        EXPECT_EQ(sel.rows.size(), 0);
    }
}

// Scenario 2: Crash after WAL persistence but before data page persistence
TEST_F(CrashRecoveryProcessTest, Scenario2_CrashAfterWalBeforeDataPageFlush) {
    ASSERT_FALSE(worker_exe_.empty()) << "ceebase_crash_worker binary not found";
    int exit_code = RunCrashWorker(worker_exe_, db_path_, 2);
    EXPECT_EQ(exit_code, 42);

    DiskManager dm;
    ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);

    std::vector<Column> cols;
    cols.emplace_back("id", DataType::INTEGER, false);
    cols.emplace_back("balance", DataType::DOUBLE, false);
    (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");

    LogManager log_mgr(wal_path_);
    RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

    // Run recovery: Redo must replay the committed transaction into data pages
    auto rec_status = recovery.Recover();
    EXPECT_TRUE(rec_status.ok());

    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    auto sel = engine.Execute("SELECT * FROM ledger;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), 1);
    EXPECT_EQ(sel.rows[0][0], "20");
    EXPECT_EQ(sel.rows[0][1], "1000.000000");

    // Primary key index must also be recovered and retrievable
    auto* pk_idx = catalog.GetIndex("ledger_pk_idx");
    ASSERT_NE(pk_idx, nullptr);
    RID rid;
    EXPECT_TRUE(pk_idx->index->GetValue(20, &rid));
}

// Scenario 3: Crash during active transaction (uncommitted transaction rollback)
TEST_F(CrashRecoveryProcessTest, Scenario3_CrashDuringActiveTransaction) {
    ASSERT_FALSE(worker_exe_.empty()) << "ceebase_crash_worker binary not found";
    int exit_code = RunCrashWorker(worker_exe_, db_path_, 3);
    EXPECT_EQ(exit_code, 42);

    DiskManager dm;
    ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);

    std::vector<Column> cols;
    cols.emplace_back("id", DataType::INTEGER, false);
    cols.emplace_back("balance", DataType::DOUBLE, false);
    (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");

    LogManager log_mgr(wal_path_);
    RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

    auto rec_status = recovery.Recover();
    EXPECT_TRUE(rec_status.ok());

    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    // Txn 1 (row 1) must be present, Txn 2 (rows 2 and 3) must be rolled back!
    auto sel = engine.Execute("SELECT * FROM ledger ORDER BY id ASC;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), 1);
    EXPECT_EQ(sel.rows[0][0], "1");
    EXPECT_EQ(sel.rows[0][1], "100.000000");

    auto* pk_idx = catalog.GetIndex("ledger_pk_idx");
    ASSERT_NE(pk_idx, nullptr);
    RID rid;
    EXPECT_TRUE(pk_idx->index->GetValue(1, &rid));
    EXPECT_FALSE(pk_idx->index->GetValue(2, &rid));
    EXPECT_FALSE(pk_idx->index->GetValue(3, &rid));
}

// Scenario 4: Crash immediately after commit durability
TEST_F(CrashRecoveryProcessTest, Scenario4_CrashImmediatelyAfterCommitDurability) {
    ASSERT_FALSE(worker_exe_.empty()) << "ceebase_crash_worker binary not found";
    int exit_code = RunCrashWorker(worker_exe_, db_path_, 4);
    EXPECT_EQ(exit_code, 42);

    DiskManager dm;
    ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);

    std::vector<Column> cols;
    cols.emplace_back("id", DataType::INTEGER, false);
    cols.emplace_back("balance", DataType::DOUBLE, false);
    (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");

    LogManager log_mgr(wal_path_);
    RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

    auto rec_status = recovery.Recover();
    EXPECT_TRUE(rec_status.ok());

    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    // Row 1 updated to 999.0, Row 2 inserted with 500.0
    auto sel = engine.Execute("SELECT * FROM ledger ORDER BY id ASC;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), 2);
    EXPECT_EQ(sel.rows[0][0], "1");
    EXPECT_EQ(sel.rows[0][1], "999.000000");
    EXPECT_EQ(sel.rows[1][0], "2");
    EXPECT_EQ(sel.rows[1][1], "500.000000");
}

// Scenario 5: Crash during recovery itself
TEST_F(CrashRecoveryProcessTest, Scenario5_CrashDuringRecovery) {
    ASSERT_FALSE(worker_exe_.empty()) << "ceebase_crash_worker binary not found";
    int exit_code = RunCrashWorker(worker_exe_, db_path_, 5);
    EXPECT_EQ(exit_code, 42);

    // Reopen and run normal recovery to completion
    DiskManager dm;
    ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
    BufferPoolManager bpm(10, &dm);
    Catalog catalog(&bpm);

    std::vector<Column> cols;
    cols.emplace_back("id", DataType::INTEGER, false);
    cols.emplace_back("balance", DataType::DOUBLE, false);
    (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");

    LogManager log_mgr(wal_path_);
    RecoveryEngine recovery(&log_mgr, &bpm, &catalog);

    // Must be able to recover successfully after an interrupted recovery
    auto rec_status = recovery.Recover();
    EXPECT_TRUE(rec_status.ok());

    TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
    ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

    auto sel = engine.Execute("SELECT * FROM ledger;");
    ASSERT_TRUE(sel.success);
    ASSERT_EQ(sel.rows.size(), 1);
    EXPECT_EQ(sel.rows[0][0], "1");
}

// Scenario 6: Repeated recovery attempts on the same state (idempotence)
TEST_F(CrashRecoveryProcessTest, Scenario6_RepeatedRecoveryIdempotence) {
    ASSERT_FALSE(worker_exe_.empty()) << "ceebase_crash_worker binary not found";
    int exit_code = RunCrashWorker(worker_exe_, db_path_, 4);
    EXPECT_EQ(exit_code, 42);

    // Recovery attempt 1
    {
        DiskManager dm;
        ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
        BufferPoolManager bpm(10, &dm);
        Catalog catalog(&bpm);
        std::vector<Column> cols;
        cols.emplace_back("id", DataType::INTEGER, false);
        cols.emplace_back("balance", DataType::DOUBLE, false);
        (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");
        LogManager log_mgr(wal_path_);
        RecoveryEngine recovery(&log_mgr, &bpm, &catalog);
        ASSERT_TRUE(recovery.Recover().ok());
    }

    // Recovery attempt 2 on the same database and WAL
    {
        DiskManager dm;
        ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
        BufferPoolManager bpm(10, &dm);
        Catalog catalog(&bpm);
        std::vector<Column> cols;
        cols.emplace_back("id", DataType::INTEGER, false);
        cols.emplace_back("balance", DataType::DOUBLE, false);
        (void)catalog.RegisterExistingTable("ledger", Schema(std::move(cols)), 1, "id");
        LogManager log_mgr(wal_path_);
        RecoveryEngine recovery(&log_mgr, &bpm, &catalog);
        auto rec2 = recovery.Recover();
        EXPECT_TRUE(rec2.ok());

        TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        auto sel = engine.Execute("SELECT * FROM ledger ORDER BY id ASC;");
        ASSERT_TRUE(sel.success);
        ASSERT_EQ(sel.rows.size(), 2);
        EXPECT_EQ(sel.rows[0][0], "1");
        EXPECT_EQ(sel.rows[0][1], "999.000000");
        EXPECT_EQ(sel.rows[1][0], "2");
        EXPECT_EQ(sel.rows[1][1], "500.000000");
    }
}

} // namespace
} // namespace ceebase
