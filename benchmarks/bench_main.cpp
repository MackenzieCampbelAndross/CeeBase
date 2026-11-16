#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/common/config.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/recovery/log_manager.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace {

using Clock = std::chrono::high_resolution_clock;

struct BenchmarkMetrics {
    double total_ms{0.0};
    double throughput_ops_sec{0.0};
    double avg_us{0.0};
    double p50_us{0.0};
    double p95_us{0.0};
    double p99_us{0.0};
};

BenchmarkMetrics ComputeMetrics(std::vector<double>& latencies_us, size_t total_ops, double total_ms) {
    BenchmarkMetrics m;
    m.total_ms = total_ms;
    m.throughput_ops_sec = (static_cast<double>(total_ops) / (total_ms / 1000.0));

    if (latencies_us.empty()) return m;

    std::sort(latencies_us.begin(), latencies_us.end());
    double sum = 0.0;
    for (double lat : latencies_us) sum += lat;
    m.avg_us = sum / static_cast<double>(latencies_us.size());

    size_t idx50 = static_cast<size_t>(static_cast<double>(latencies_us.size()) * 0.50);
    size_t idx95 = static_cast<size_t>(static_cast<double>(latencies_us.size()) * 0.95);
    size_t idx99 = static_cast<size_t>(static_cast<double>(latencies_us.size()) * 0.99);

    if (idx50 >= latencies_us.size()) idx50 = latencies_us.size() - 1;
    if (idx95 >= latencies_us.size()) idx95 = latencies_us.size() - 1;
    if (idx99 >= latencies_us.size()) idx99 = latencies_us.size() - 1;

    m.p50_us = latencies_us[idx50];
    m.p95_us = latencies_us[idx95];
    m.p99_us = latencies_us[idx99];
    return m;
}

void PrintMetricsRow(std::string_view workload, const BenchmarkMetrics& m) {
    std::cout << "  " << std::left << std::setw(28) << workload
              << " | " << std::right << std::setw(10) << std::fixed << std::setprecision(0) << m.throughput_ops_sec << " ops/s"
              << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << m.avg_us << " us"
              << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << m.p50_us << " us"
              << " | " << std::right << std::setw(8) << std::fixed << std::setprecision(1) << m.p95_us << " us\n";
}

void RunBenchmarkScale(size_t scale) {
    std::cout << "\n===============================================================================\n";
    std::cout << " BENCHMARK WORKLOAD SCALE: " << scale << " RECORDS\n";
    std::cout << "===============================================================================\n";

    auto bench_dir = std::filesystem::temp_directory_path() / ("ceebase_bench_" + std::to_string(scale));
    std::filesystem::create_directories(bench_dir);
    auto db_path = bench_dir / "bench.nxdb";
    auto wal_path = bench_dir / "bench.wal";
    std::filesystem::remove(db_path);
    std::filesystem::remove(wal_path);

    {
        ceebase::DiskManager dm;
        (void)dm.CreateDatabase(db_path);

        size_t pool_size = std::max<size_t>(64, scale / 10);
        ceebase::LogManager log_mgr(wal_path);
        ceebase::BufferPoolManager bpm(pool_size, &dm, &log_mgr);
        ceebase::Catalog catalog(&bpm);
        ceebase::TransactionManager txn_mgr(&log_mgr, &bpm, &catalog);
        ceebase::ExecutionEngine engine(&bpm, &catalog, &txn_mgr);

        (void)engine.Execute("CREATE TABLE bench (id INTEGER PRIMARY KEY, score DOUBLE, note TEXT);");

        std::cout << "  Configuration:\n";
        std::cout << "    Page Size:       " << ceebase::PAGE_SIZE << " bytes\n";
        std::cout << "    Buffer Pool:     " << pool_size << " frames (" << (pool_size * ceebase::PAGE_SIZE) / 1024 << " KB)\n";
        std::cout << "    Durability:      OS Synchronized WAL (FlushFileBuffers/fsync)\n\n";

        std::cout << "  " << std::left << std::setw(28) << "Operation"
                  << " | " << std::right << std::setw(16) << "Throughput"
                  << " | " << std::right << std::setw(11) << "Avg Latency"
                  << " | " << std::right << std::setw(11) << "p50 Latency"
                  << " | " << std::right << std::setw(11) << "p95 Latency\n";
        std::cout << "  -----------------------------------------------------------------------------\n";

        // 1. Sequential Inserts (in batches of 500 or single txn)
        std::vector<double> insert_latencies_us;
        insert_latencies_us.reserve(scale);

        auto t_ins_start = Clock::now();
        size_t batch_size = 500;
        for (size_t b = 0; b < scale; b += batch_size) {
            size_t cur_batch = std::min(batch_size, scale - b);
            (void)engine.Execute("BEGIN;");
            for (size_t i = 0; i < cur_batch; ++i) {
                size_t id = b + i;
                auto t0 = Clock::now();
                std::string sql = "INSERT INTO bench VALUES (" + std::to_string(id) + ", " +
                                  std::to_string(static_cast<double>(id) * 1.5) + ", 'val_" + std::to_string(id) + "');";
                (void)engine.Execute(sql);
                auto t1 = Clock::now();
                insert_latencies_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
            }
            (void)engine.Execute("COMMIT;");
        }
        auto t_ins_end = Clock::now();
        double total_ins_ms = std::chrono::duration<double, std::milli>(t_ins_end - t_ins_start).count();
        auto ins_m = ComputeMetrics(insert_latencies_us, scale, total_ins_ms);
        PrintMetricsRow("Batch Sequential Insert", ins_m);

        // 2. Point Lookups via B+ Tree Index
        size_t lookup_reps = std::min<size_t>(scale, 5000);
        std::vector<double> lookup_latencies_us;
        lookup_latencies_us.reserve(lookup_reps);

        std::mt19937 rng(42);
        std::uniform_int_distribution<size_t> id_dist(0, scale - 1);

        auto t_lookup_start = Clock::now();
        for (size_t r = 0; r < lookup_reps; ++r) {
            size_t target_id = id_dist(rng);
            auto t0 = Clock::now();
            (void)engine.Execute("SELECT * FROM bench WHERE id = " + std::to_string(target_id) + ";");
            auto t1 = Clock::now();
            lookup_latencies_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto t_lookup_end = Clock::now();
        double total_lookup_ms = std::chrono::duration<double, std::milli>(t_lookup_end - t_lookup_start).count();
        auto lookup_m = ComputeMetrics(lookup_latencies_us, lookup_reps, total_lookup_ms);
        PrintMetricsRow("Point Lookup (B+ Tree)", lookup_m);

        // 3. Sequential Table Scan
        size_t scan_reps = (scale >= 50000) ? 2 : 10;
        std::vector<double> scan_latencies_us;
        auto t_scan_start = Clock::now();
        for (size_t r = 0; r < scan_reps; ++r) {
            auto t0 = Clock::now();
            (void)engine.Execute("SELECT * FROM bench WHERE score < 0.0;"); // full scan matching 0 rows
            auto t1 = Clock::now();
            scan_latencies_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        auto t_scan_end = Clock::now();
        double total_scan_ms = std::chrono::duration<double, std::milli>(t_scan_end - t_scan_start).count();
        auto scan_m = ComputeMetrics(scan_latencies_us, scan_reps, total_scan_ms);
        PrintMetricsRow("Sequential Table Scan", scan_m);

        // 4. Point Updates
        size_t update_reps = std::min<size_t>(scale / 2, 2000);
        std::vector<double> update_latencies_us;
        update_latencies_us.reserve(update_reps);

        auto t_upd_start = Clock::now();
        (void)engine.Execute("BEGIN;");
        for (size_t r = 0; r < update_reps; ++r) {
            size_t target_id = r * 2;
            auto t0 = Clock::now();
            (void)engine.Execute("UPDATE bench SET score = 9999.0 WHERE id = " + std::to_string(target_id) + ";");
            auto t1 = Clock::now();
            update_latencies_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        (void)engine.Execute("COMMIT;");
        auto t_upd_end = Clock::now();
        double total_upd_ms = std::chrono::duration<double, std::milli>(t_upd_end - t_upd_start).count();
        auto upd_m = ComputeMetrics(update_latencies_us, update_reps, total_upd_ms);
        PrintMetricsRow("Point Update (Indexed)", upd_m);

        // 5. Point Deletes
        size_t delete_reps = std::min<size_t>(scale / 4, 1000);
        std::vector<double> delete_latencies_us;
        delete_latencies_us.reserve(delete_reps);

        auto t_del_start = Clock::now();
        (void)engine.Execute("BEGIN;");
        for (size_t r = 0; r < delete_reps; ++r) {
            size_t target_id = r * 4;
            auto t0 = Clock::now();
            (void)engine.Execute("DELETE FROM bench WHERE id = " + std::to_string(target_id) + ";");
            auto t1 = Clock::now();
            delete_latencies_us.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
        }
        (void)engine.Execute("COMMIT;");
        auto t_del_end = Clock::now();
        double total_del_ms = std::chrono::duration<double, std::milli>(t_del_end - t_del_start).count();
        auto del_m = ComputeMetrics(delete_latencies_us, delete_reps, total_del_ms);
        PrintMetricsRow("Point Delete (Indexed)", del_m);

        std::cout << "  -----------------------------------------------------------------------------\n";
        std::cout << "  Summary: B+ Tree Point Lookup vs Table Scan Speedup: "
                  << std::fixed << std::setprecision(1) << (scan_m.avg_us / lookup_m.avg_us) << "x faster\n";
    }

    std::error_code ec;
    std::filesystem::remove_all(bench_dir, ec);
}

} // namespace

int main(int argc, char** argv) {
    std::cout << "===============================================================================\n";
    std::cout << " CeeBase: RELATIONAL DATABASE PERFORMANCE & BENCHMARK SUITE\n";
    std::cout << " Implementation: First-principles C++20 | 4KB Slotted Pages | B+ Tree | WAL\n";
    std::cout << " Compiler: GCC " << __VERSION__ << " | Host Architecture: Little-Endian\n";
    std::cout << "===============================================================================\n";

    if (argc > 1) {
        size_t requested_scale = std::stoull(argv[1]);
        RunBenchmarkScale(requested_scale);
    } else {
        // Run standard benchmark scales: 500 and 10,000 (and 100,000 if requested)
        RunBenchmarkScale(500);
        RunBenchmarkScale(10000);
    }

    return 0;
}
