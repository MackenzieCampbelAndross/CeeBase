#include "nexus/recovery/recovery_engine.hpp"

namespace nexus {

Status RecoveryEngine::Recover() {
    auto records_res = log_manager_->ReadAllRecords();
    if (!records_res.ok()) {
        return records_res.status();
    }

    const auto& records = *records_res;
    if (records.empty()) {
        return Status::Ok();
    }

    // Phase 1: Analysis
    Status s1 = AnalysisPhase(records);
    if (!s1.ok()) return s1;

    // Phase 2: Redo
    Status s2 = RedoPhase(records);
    if (!s2.ok()) return s2;

    if (crash_point_ == CrashPoint::DURING_REDO) {
        return Status::Corruption("Crash simulated during Redo phase");
    }

    // Phase 3: Undo
    Status s3 = UndoPhase(records);
    if (!s3.ok()) return s3;

    // Flush all recovered pages to make database clean
    return bpm_->FlushAllPages();
}

Status RecoveryEngine::AnalysisPhase(const std::vector<LogRecord>& records) {
    active_txns_.clear();
    committed_txns_.clear();

    for (const auto& rec : records) {
        switch (rec.GetType()) {
            case LogRecordType::BEGIN:
                active_txns_.insert(rec.GetTxnId());
                break;
            case LogRecordType::COMMIT:
                active_txns_.erase(rec.GetTxnId());
                committed_txns_.insert(rec.GetTxnId());
                break;
            case LogRecordType::ABORT:
                active_txns_.erase(rec.GetTxnId());
                break;
            case LogRecordType::CHECKPOINT:
                // Checkpoint marker
                break;
            default:
                break;
        }
    }
    return Status::Ok();
}

Status RecoveryEngine::RedoPhase(const std::vector<LogRecord>& records) {
    for (const auto& rec : records) {
        // Redo modifications
        if (rec.GetType() == LogRecordType::INSERT) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (tbl && tbl->table_heap) {
                // Reapply after-image to page
                Tuple tuple(rec.GetAfterImage(), rec.GetRID());
                (void)tbl->table_heap->UpdateTuple(tuple, rec.GetRID());
            }
        } else if (rec.GetType() == LogRecordType::UPDATE) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (tbl && tbl->table_heap) {
                Tuple tuple(rec.GetAfterImage(), rec.GetRID());
                (void)tbl->table_heap->UpdateTuple(tuple, rec.GetRID());
            }
        } else if (rec.GetType() == LogRecordType::DELETE_REC) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (tbl && tbl->table_heap) {
                (void)tbl->table_heap->MarkDelete(rec.GetRID());
            }
        }
    }
    return Status::Ok();
}

Status RecoveryEngine::UndoPhase(const std::vector<LogRecord>& records) {
    // Traverse backwards to rollback active/uncommitted transactions
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& rec = *it;
        if (active_txns_.find(rec.GetTxnId()) != active_txns_.end()) {
            if (rec.GetType() == LogRecordType::INSERT) {
                // Undo insert by marking deleted
                auto* tbl = catalog_->GetTable(rec.GetTableName());
                if (tbl && tbl->table_heap) {
                    (void)tbl->table_heap->MarkDelete(rec.GetRID());
                }
            } else if (rec.GetType() == LogRecordType::UPDATE) {
                // Undo update by restoring before image
                auto* tbl = catalog_->GetTable(rec.GetTableName());
                if (tbl && tbl->table_heap) {
                    Tuple before_tuple(rec.GetBeforeImage(), rec.GetRID());
                    (void)tbl->table_heap->UpdateTuple(before_tuple, rec.GetRID());
                }
            } else if (rec.GetType() == LogRecordType::DELETE_REC) {
                // Undo delete by restoring deleted tuple
                auto* tbl = catalog_->GetTable(rec.GetTableName());
                if (tbl && tbl->table_heap) {
                    Tuple before_tuple(rec.GetBeforeImage(), rec.GetRID());
                    (void)tbl->table_heap->UpdateTuple(before_tuple, rec.GetRID());
                }
            }
        }
    }

    // Write ABORT records for all undone transactions
    for (txn_id_t tid : active_txns_) {
        LogRecord abort_rec(tid, INVALID_LSN, LogRecordType::ABORT);
        (void)log_manager_->AppendRecord(&abort_rec);
    }
    (void)log_manager_->Flush();
    active_txns_.clear();

    return Status::Ok();
}

} // namespace nexus
