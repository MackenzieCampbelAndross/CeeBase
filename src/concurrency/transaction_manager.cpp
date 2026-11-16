#include "nexus/concurrency/transaction_manager.hpp"

#include "nexus/catalog/catalog.hpp"

namespace nexus {

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
        // WAL protocol: Commit record MUST be durably flushed before commit completes
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

    // Rollback inserts (delete inserted tuples)
    const auto& inserts = txn->GetInsertRecords();
    for (auto it = inserts.rbegin(); it != inserts.rend(); ++it) {
        auto* tbl = it->first;
        RID rid = it->second;
        if (tbl && tbl->table_heap) {
            (void)tbl->table_heap->MarkDelete(rid);
        }
    }

    // Rollback updates (restore before-image tuples)
    const auto& updates = txn->GetUpdateRecords();
    for (auto it = updates.rbegin(); it != updates.rend(); ++it) {
        auto* tbl = std::get<0>(*it);
        RID rid = std::get<1>(*it);
        const Tuple& before_image = std::get<2>(*it);
        if (tbl && tbl->table_heap) {
            (void)tbl->table_heap->UpdateTuple(before_image, rid);
        }
    }

    // Rollback deletes (re-insert deleted tuples)
    const auto& deletes = txn->GetDeleteRecords();
    for (auto it = deletes.rbegin(); it != deletes.rend(); ++it) {
        auto* tbl = it->first;
        const Tuple& deleted_tuple = it->second;
        if (tbl && tbl->table_heap) {
            RID reinserted_rid;
            (void)tbl->table_heap->InsertTuple(deleted_tuple, &reinserted_rid);
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

} // namespace nexus
