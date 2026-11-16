#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "nexus/buffer/buffer_pool_manager.hpp"
#include "nexus/concurrency/transaction.hpp"
#include "nexus/recovery/log_manager.hpp"

namespace nexus {

class TransactionManager {
public:
    TransactionManager(LogManager* log_manager, BufferPoolManager* bpm)
        : log_manager_(log_manager), bpm_(bpm) {}
    ~TransactionManager() = default;

    // Transaction Lifecycle
    std::unique_ptr<Transaction> Begin();
    Status Commit(Transaction* txn);
    Status Abort(Transaction* txn);

    // Checkpointing: Flush dirty pages and record checkpoint in WAL
    Status Checkpoint();

    [[nodiscard]] LogManager* GetLogManager() const noexcept { return log_manager_; }

private:
    LogManager* log_manager_;
    BufferPoolManager* bpm_;
    std::atomic<txn_id_t> next_txn_id_{1};
    mutable std::mutex latch_{};
};

} // namespace nexus
