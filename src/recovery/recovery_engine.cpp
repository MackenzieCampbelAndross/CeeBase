#include "ceebase/recovery/recovery_engine.hpp"

#include "ceebase/record/slotted_page.hpp"

namespace ceebase {

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
                break;
            default:
                break;
        }
    }
    return Status::Ok();
}

Status RecoveryEngine::RedoPhase(const std::vector<LogRecord>& records) {
    for (const auto& rec : records) {
        if (rec.GetType() == LogRecordType::INSERT) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (!tbl || !tbl->table_heap) continue;

            // Idempotency check: inspect page LSN
            page_id_t pid = rec.GetRID().page_id;
            auto page_res = bpm_->FetchPage(pid);
            if (!page_res.ok()) continue;

            Page* page = *page_res;
            if (page->GetLSN() >= rec.GetLSN()) {
                (void)bpm_->UnpinPage(pid, false);
                continue;
            }

            // Apply after-image
            Tuple tuple(rec.GetAfterImage(), rec.GetRID());
            (void)tbl->table_heap->WriteTupleAtRID(tuple, rec.GetRID());

            page->SetLSN(rec.GetLSN());
            SlottedPage(page->GetData()).SetLSN(rec.GetLSN());
            (void)bpm_->UnpinPage(pid, true);

            // Replay index insertion
            auto indexes = catalog_->GetTableIndexes(tbl->name);
            for (auto* idx_meta : indexes) {
                int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                if (col_idx != -1) {
                    Value key_val = tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                    int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                    (void)idx_meta->index->Insert(k, rec.GetRID());
                }
            }
        } else if (rec.GetType() == LogRecordType::UPDATE) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (!tbl || !tbl->table_heap) continue;

            page_id_t pid = rec.GetRID().page_id;
            auto page_res = bpm_->FetchPage(pid);
            if (!page_res.ok()) continue;

            Page* page = *page_res;
            if (page->GetLSN() >= rec.GetLSN()) {
                (void)bpm_->UnpinPage(pid, false);
                continue;
            }

            Tuple old_tuple(rec.GetBeforeImage(), rec.GetRID());
            Tuple new_tuple(rec.GetAfterImage(), rec.GetRID());
            (void)tbl->table_heap->WriteTupleAtRID(new_tuple, rec.GetRID());

            page->SetLSN(rec.GetLSN());
            SlottedPage(page->GetData()).SetLSN(rec.GetLSN());
            (void)bpm_->UnpinPage(pid, true);

            // Replay index update
            auto indexes = catalog_->GetTableIndexes(tbl->name);
            for (auto* idx_meta : indexes) {
                int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                if (col_idx != -1) {
                    Value old_val = old_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                    Value new_val = new_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                    int64_t old_k = (old_val.GetType() == DataType::INTEGER) ? old_val.GetInteger() : old_val.GetBigInt();
                    int64_t new_k = (new_val.GetType() == DataType::INTEGER) ? new_val.GetInteger() : new_val.GetBigInt();
                    if (old_k != new_k) {
                        (void)idx_meta->index->Remove(old_k);
                        (void)idx_meta->index->Insert(new_k, rec.GetRID());
                    }
                }
            }
        } else if (rec.GetType() == LogRecordType::DELETE_REC) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (!tbl || !tbl->table_heap) continue;

            page_id_t pid = rec.GetRID().page_id;
            auto page_res = bpm_->FetchPage(pid);
            if (!page_res.ok()) continue;

            Page* page = *page_res;
            if (page->GetLSN() >= rec.GetLSN()) {
                (void)bpm_->UnpinPage(pid, false);
                continue;
            }

            (void)tbl->table_heap->MarkDelete(rec.GetRID());

            page->SetLSN(rec.GetLSN());
            SlottedPage(page->GetData()).SetLSN(rec.GetLSN());
            (void)bpm_->UnpinPage(pid, true);

            // Replay index deletion
            Tuple old_tuple(rec.GetBeforeImage(), rec.GetRID());
            auto indexes = catalog_->GetTableIndexes(tbl->name);
            for (auto* idx_meta : indexes) {
                int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                if (col_idx != -1) {
                    Value key_val = old_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                    int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                    (void)idx_meta->index->Remove(k);
                }
            }
        }
    }
    return Status::Ok();
}

Status RecoveryEngine::UndoPhase(const std::vector<LogRecord>& records) {
    // Traverse backwards to rollback active/uncommitted (loser) transactions
    for (auto it = records.rbegin(); it != records.rend(); ++it) {
        const auto& rec = *it;
        if (active_txns_.find(rec.GetTxnId()) != active_txns_.end()) {
            auto* tbl = catalog_->GetTable(rec.GetTableName());
            if (!tbl || !tbl->table_heap) continue;

            if (rec.GetType() == LogRecordType::INSERT) {
                // Undo insert by marking deleted
                (void)tbl->table_heap->MarkDelete(rec.GetRID());

                // Remove from indexes
                Tuple tuple(rec.GetAfterImage(), rec.GetRID());
                auto indexes = catalog_->GetTableIndexes(tbl->name);
                for (auto* idx_meta : indexes) {
                    int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                    if (col_idx != -1) {
                        Value key_val = tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                        (void)idx_meta->index->Remove(k);
                    }
                }
            } else if (rec.GetType() == LogRecordType::UPDATE) {
                // Undo update by restoring before image
                Tuple before_tuple(rec.GetBeforeImage(), rec.GetRID());
                (void)tbl->table_heap->WriteTupleAtRID(before_tuple, rec.GetRID());

                Tuple after_tuple(rec.GetAfterImage(), rec.GetRID());
                auto indexes = catalog_->GetTableIndexes(tbl->name);
                for (auto* idx_meta : indexes) {
                    int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                    if (col_idx != -1) {
                        Value before_val = before_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        Value after_val = after_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        int64_t before_k = (before_val.GetType() == DataType::INTEGER) ? before_val.GetInteger() : before_val.GetBigInt();
                        int64_t after_k = (after_val.GetType() == DataType::INTEGER) ? after_val.GetInteger() : after_val.GetBigInt();
                        if (before_k != after_k) {
                            (void)idx_meta->index->Remove(after_k);
                            (void)idx_meta->index->Insert(before_k, rec.GetRID());
                        }
                    }
                }
            } else if (rec.GetType() == LogRecordType::DELETE_REC) {
                // Undo delete by restoring deleted tuple
                Tuple before_tuple(rec.GetBeforeImage(), rec.GetRID());
                (void)tbl->table_heap->WriteTupleAtRID(before_tuple, rec.GetRID());

                auto indexes = catalog_->GetTableIndexes(tbl->name);
                for (auto* idx_meta : indexes) {
                    int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                    if (col_idx != -1) {
                        Value key_val = before_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                        (void)idx_meta->index->Insert(k, rec.GetRID());
                    }
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

} // namespace ceebase
