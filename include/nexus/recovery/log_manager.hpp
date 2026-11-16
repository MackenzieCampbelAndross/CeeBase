#pragma once

#include <atomic>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <vector>

#include "nexus/common/result.hpp"
#include "nexus/common/status.hpp"
#include "nexus/recovery/log_record.hpp"

namespace nexus {

class LogManager {
public:
    explicit LogManager(std::filesystem::path wal_path);
    ~LogManager();

    LogManager(const LogManager&) = delete;
    LogManager& operator=(const LogManager&) = delete;

    // Append log record to memory buffer and assign persistent LSN
    lsn_t AppendRecord(LogRecord* record);

    // Force flush log buffer to physical WAL disk file
    Status Flush();

    // Read all records from the WAL file on disk (for recovery)
    Result<std::vector<LogRecord>> ReadAllRecords();

    [[nodiscard]] lsn_t GetNextLSN() const noexcept { return next_lsn_.load(); }
    [[nodiscard]] lsn_t GetPersistedLSN() const noexcept { return persisted_lsn_.load(); }
    [[nodiscard]] const std::filesystem::path& GetWalPath() const noexcept { return wal_path_; }

    void Clear();

private:
    std::filesystem::path wal_path_;
    FILE* wal_file_{nullptr};
    std::vector<uint8_t> buffer_{};
    std::atomic<lsn_t> next_lsn_{1};
    std::atomic<lsn_t> persisted_lsn_{0};
    mutable std::mutex latch_{};
};

} // namespace nexus
