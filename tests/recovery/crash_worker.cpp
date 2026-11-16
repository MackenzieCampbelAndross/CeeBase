#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#include "nexus/catalog/catalog.hpp"
#include "nexus/concurrency/transaction_manager.hpp"
#include "nexus/execution/execution_engine.hpp"
#include "nexus/recovery/log_manager.hpp"
#include "nexus/recovery/recovery_engine.hpp"
#include "nexus/storage/disk_manager.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <unistd.h>
#endif

// Abruptly terminate process without calling C++ destructors or atexit handlers
[[noreturn]] void AbruptCrash(int exit_code = 42) {
#ifdef _WIN32
    ExitProcess(static_cast<UINT>(exit_code));
#else
    _exit(exit_code);
#endif
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <db_path> <scenario_id>\n";
        return 1;
    }

    std::filesystem::path db_path = argv[1];
    int scenario = std::stoi(argv[2]);

    std::filesystem::path wal_path = db_path;
    wal_path.replace_extension(".wal");

    if (scenario == 1) {
        // Scenario 1: Crash before WAL persistence
        nexus::DiskManager dm;
        (void)dm.CreateDatabase(db_path);
        nexus::BufferPoolManager bpm(10, &dm);
        nexus::Catalog catalog(&bpm);
        nexus::LogManager log_mgr(wal_path);
        nexus::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        nexus::ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        (void)engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, balance DOUBLE);");
        (void)engine.Execute("BEGIN;");
        // Insert without commit or WAL flush
        (void)engine.Execute("INSERT INTO ledger VALUES (10, 500.0);");

        // Crash abruptly without flushing WAL
        AbruptCrash(42);
    } else if (scenario == 2) {
        // Scenario 2: Crash after WAL persistence but before data page persistence
        nexus::DiskManager dm;
        (void)dm.CreateDatabase(db_path);
        nexus::BufferPoolManager bpm(10, &dm);
        nexus::Catalog catalog(&bpm);
        nexus::LogManager log_mgr(wal_path);
        bpm.SetLogManager(&log_mgr);
        nexus::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        nexus::ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        (void)engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, balance DOUBLE);");
        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("INSERT INTO ledger VALUES (20, 1000.0);");
        // Commit ensures WAL is durably synchronized to disk
        (void)engine.Execute("COMMIT;");

        // Crash immediately: pages are dirty in buffer pool and NOT flushed to database file!
        AbruptCrash(42);
    } else if (scenario == 3) {
        // Scenario 3: Crash during active transaction (uncommitted)
        nexus::DiskManager dm;
        (void)dm.CreateDatabase(db_path);
        nexus::BufferPoolManager bpm(10, &dm);
        nexus::Catalog catalog(&bpm);
        nexus::LogManager log_mgr(wal_path);
        bpm.SetLogManager(&log_mgr);
        nexus::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        nexus::ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        (void)engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, balance DOUBLE);");
        // Txn 1: Committed
        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("INSERT INTO ledger VALUES (1, 100.0);");
        (void)engine.Execute("COMMIT;");

        // Txn 2: Uncommitted
        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("INSERT INTO ledger VALUES (2, 200.0);");
        (void)engine.Execute("INSERT INTO ledger VALUES (3, 300.0);");

        // Crash abruptly while Txn 2 is active
        AbruptCrash(42);
    } else if (scenario == 4) {
        // Scenario 4: Crash immediately after commit durability
        nexus::DiskManager dm;
        (void)dm.CreateDatabase(db_path);
        nexus::BufferPoolManager bpm(10, &dm);
        nexus::Catalog catalog(&bpm);
        nexus::LogManager log_mgr(wal_path);
        bpm.SetLogManager(&log_mgr);
        nexus::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        nexus::ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        (void)engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, balance DOUBLE);");
        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("INSERT INTO ledger VALUES (1, 100.0);");
        (void)engine.Execute("COMMIT;");

        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("UPDATE ledger SET balance = 999.0 WHERE id = 1;");
        (void)engine.Execute("COMMIT;");

        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("INSERT INTO ledger VALUES (2, 500.0);");
        (void)engine.Execute("COMMIT;");

        // Durably committed all three, crash abruptly
        AbruptCrash(42);
    } else if (scenario == 5) {
        // Scenario 5: Crash during recovery itself
        nexus::DiskManager dm;
        (void)dm.CreateDatabase(db_path);
        nexus::BufferPoolManager bpm(10, &dm);
        nexus::Catalog catalog(&bpm);
        nexus::LogManager log_mgr(wal_path);
        bpm.SetLogManager(&log_mgr);
        nexus::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        nexus::ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        (void)engine.Execute("CREATE TABLE ledger (id INTEGER PRIMARY KEY, balance DOUBLE);");
        (void)engine.Execute("BEGIN;");
        (void)engine.Execute("INSERT INTO ledger VALUES (1, 100.0);");
        (void)engine.Execute("COMMIT;");

        nexus::RecoveryEngine recovery(&log_mgr, &bpm, &catalog);
        recovery.SetCrashPoint(nexus::CrashPoint::DURING_REDO);
        auto rec_status = recovery.Recover();
        if (!rec_status.ok()) {
            // Simulated crash
            AbruptCrash(42);
        }
    }

    return 0;
}
