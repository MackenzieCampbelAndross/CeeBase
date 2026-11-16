#include "ceebase/concurrency/transaction_manager.hpp"

#include "ceebase/catalog/catalog.hpp"

namespace ceebase {

std::unique_ptr<Transaction> TransactionManager::Begin() {
    std::lock_guard<std::mutex> guard(latch_);
    txn_id_t tid = next_txn_id_++;
    auto txn = std::make_unique<Transaction>(tid);

    if (log_manager_) {
        LogRecord begin_record(tid, INVALID_LSN, LogRecordType::BEGIN);
        lsn_t lsn = log_manager_->AppendRecord(&begin_record);
        txn->SetPrevLSN(lsn);
    }

    return txn;
}

Status TransactionManager::Commit(Transaction* txn) {
    std::lock_guard<std::mutex> guard(latch_);
    if (!txn || txn->GetState() != TransactionState::RUNNING) {
        return Status::InvalidArgument("TransactionManager: Invalid transaction state for commit");
    }

    if (log_manager_) {
        LogRecord commit_record(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::COMMIT);
        (void)log_manager_->AppendRecord(&commit_record);
        // WAL protocol: Commit record MUST be durably flushed to physical disk before commit completes
        Status s = log_manager_->Flush();
        if (!s.ok()) {
            return s;
        }
    }

    txn->SetState(TransactionState::COMMITTED);
    return Status::Ok();
}

Status TransactionManager::Abort(Transaction* txn) {
    std::lock_guard<std::mutex> guard(latch_);
    if (!txn || txn->GetState() != TransactionState::RUNNING) {
        return Status::InvalidArgument("TransactionManager: Invalid transaction state for abort");
    }

    // Rollback inserts (delete inserted tuples and remove from indexes)
    const auto& inserts = txn->GetInsertRecords();
    for (auto it = inserts.rbegin(); it != inserts.rend(); ++it) {
        auto* tbl = it->first;
        RID rid = it->second;
        if (tbl && tbl->table_heap) {
            auto tup_res = tbl->table_heap->GetTuple(rid);
            if (tup_res.ok() && catalog_) {
                auto indexes = catalog_->GetTableIndexes(tbl->name);
                for (auto* idx_meta : indexes) {
                    int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                    if (col_idx != -1) {
                        Value key_val = tup_res->GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                        (void)idx_meta->index->Remove(k);
                    }
                }
            }
            (void)tbl->table_heap->MarkDelete(rid);
        }
    }

    // Rollback updates (restore before-image tuples and fix indexes)
    const auto& updates = txn->GetUpdateRecords();
    for (auto it = updates.rbegin(); it != updates.rend(); ++it) {
        auto* tbl = std::get<0>(*it);
        RID rid = std::get<1>(*it);
        const Tuple& before_image = std::get<2>(*it);
        if (tbl && tbl->table_heap) {
            auto cur_res = tbl->table_heap->GetTuple(rid);
            if (cur_res.ok() && catalog_) {
                auto indexes = catalog_->GetTableIndexes(tbl->name);
                for (auto* idx_meta : indexes) {
                    int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                    if (col_idx != -1) {
                        Value cur_val = cur_res->GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        Value old_val = before_image.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        int64_t cur_k = (cur_val.GetType() == DataType::INTEGER) ? cur_val.GetInteger() : cur_val.GetBigInt();
                        int64_t old_k = (old_val.GetType() == DataType::INTEGER) ? old_val.GetInteger() : old_val.GetBigInt();
                        if (cur_k != old_k) {
                            (void)idx_meta->index->Remove(cur_k);
                            (void)idx_meta->index->Insert(old_k, rid);
                        }
                    }
                }
            }
            (void)tbl->table_heap->UpdateTuple(before_image, rid);
        }
    }

    // Rollback deletes (restore deleted tuples and reinsert into indexes)
    const auto& deletes = txn->GetDeleteRecords();
    for (auto it = deletes.rbegin(); it != deletes.rend(); ++it) {
        auto* tbl = std::get<0>(*it);
        RID rid = std::get<1>(*it);
        const Tuple& deleted_tuple = std::get<2>(*it);
        if (tbl && tbl->table_heap) {
            (void)tbl->table_heap->WriteTupleAtRID(deleted_tuple, rid);
            if (catalog_) {
                auto indexes = catalog_->GetTableIndexes(tbl->name);
                for (auto* idx_meta : indexes) {
                    int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
                    if (col_idx != -1) {
                        Value key_val = deleted_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                        int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                        (void)idx_meta->index->Insert(k, rid);
                    }
                }
            }
        }
    }

    if (log_manager_) {
        LogRecord abort_record(txn->GetTxnId(), txn->GetPrevLSN(), LogRecordType::ABORT);
        (void)log_manager_->AppendRecord(&abort_record);
        (void)log_manager_->Flush();
    }

    txn->SetState(TransactionState::ABORTED);
    return Status::Ok();
}

Status TransactionManager::Checkpoint() {
    std::lock_guard<std::mutex> guard(latch_);
    if (bpm_) {
        Status s = bpm_->FlushAllPages();
        if (!s.ok()) return s;
    }

    if (log_manager_) {
        LogRecord ckpt_record(0, INVALID_LSN, LogRecordType::CHECKPOINT);
        (void)log_manager_->AppendRecord(&ckpt_record);
        return log_manager_->Flush();
    }
    return Status::Ok();
}

} // namespace ceebase
