#pragma once

#include <unordered_set>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/recovery/log_manager.hpp"

namespace ceebase {

enum class CrashPoint {
    NONE = 0,
    BEFORE_COMMIT,
    AFTER_COMMIT_BEFORE_FLUSH,
    DURING_REDO
};

class RecoveryEngine {
public:
    RecoveryEngine(LogManager* log_manager, BufferPoolManager* bpm, Catalog* catalog)
        : log_manager_(log_manager), bpm_(bpm), catalog_(catalog) {}

    // Main Crash Recovery routine: executes Analysis, Redo, and Undo
    Status Recover();

    // Controlled crash simulation injection
    void SetCrashPoint(CrashPoint cp) noexcept { crash_point_ = cp; }
    [[nodiscard]] CrashPoint GetCrashPoint() const noexcept { return crash_point_; }

    [[nodiscard]] size_t GetActiveTxnCount() const noexcept { return active_txns_.size(); }
    [[nodiscard]] size_t GetCommittedTxnCount() const noexcept { return committed_txns_.size(); }

private:
    Status AnalysisPhase(const std::vector<LogRecord>& records);
    Status RedoPhase(const std::vector<LogRecord>& records);
    Status UndoPhase(const std::vector<LogRecord>& records);

    LogManager* log_manager_;
    BufferPoolManager* bpm_;
    Catalog* catalog_;
    CrashPoint crash_point_{CrashPoint::NONE};

    std::unordered_set<txn_id_t> active_txns_{};
    std::unordered_set<txn_id_t> committed_txns_{};
};

} // namespace ceebase
