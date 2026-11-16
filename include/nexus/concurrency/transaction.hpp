#pragma once

#include <atomic>
#include <cstdint>
#include <string_view>
#include <tuple>
#include <vector>

#include "nexus/common/types.hpp"
#include "nexus/record/tuple.hpp"

namespace nexus {

class TableMetadata;

enum class TransactionState {
    RUNNING,
    COMMITTED,
    ABORTED
};

[[nodiscard]] constexpr std::string_view TransactionStateToString(TransactionState state) noexcept {
    switch (state) {
        case TransactionState::RUNNING: return "RUNNING";
        case TransactionState::COMMITTED: return "COMMITTED";
        case TransactionState::ABORTED: return "ABORTED";
    }
    return "UNKNOWN";
}

class Transaction {
public:
    explicit Transaction(txn_id_t txn_id)
        : txn_id_(txn_id), state_(TransactionState::RUNNING), prev_lsn_(INVALID_LSN) {}

    [[nodiscard]] txn_id_t GetTxnId() const noexcept { return txn_id_; }
    [[nodiscard]] TransactionState GetState() const noexcept { return state_; }
    void SetState(TransactionState state) noexcept { state_ = state; }

    [[nodiscard]] lsn_t GetPrevLSN() const noexcept { return prev_lsn_; }
    void SetPrevLSN(lsn_t lsn) noexcept { prev_lsn_ = lsn; }

    // Undo set tracking
    void RecordInsert(TableMetadata* tbl, RID rid) {
        insert_records_.emplace_back(tbl, rid);
    }

    void RecordUpdate(TableMetadata* tbl, RID rid, Tuple before_image) {
        update_records_.emplace_back(tbl, rid, std::move(before_image));
    }

    void RecordDelete(TableMetadata* tbl, RID rid, Tuple deleted_tuple) {
        delete_records_.emplace_back(tbl, rid, std::move(deleted_tuple));
    }

    void RecordDelete(TableMetadata* tbl, Tuple deleted_tuple) {
        delete_records_.emplace_back(tbl, deleted_tuple.GetRID(), std::move(deleted_tuple));
    }

    [[nodiscard]] const std::vector<std::pair<TableMetadata*, RID>>& GetInsertRecords() const noexcept {
        return insert_records_;
    }

    [[nodiscard]] const std::vector<std::tuple<TableMetadata*, RID, Tuple>>& GetUpdateRecords() const noexcept {
        return update_records_;
    }

    [[nodiscard]] const std::vector<std::tuple<TableMetadata*, RID, Tuple>>& GetDeleteRecords() const noexcept {
        return delete_records_;
    }

private:
    txn_id_t txn_id_;
    TransactionState state_;
    lsn_t prev_lsn_{INVALID_LSN};

    std::vector<std::pair<TableMetadata*, RID>> insert_records_{};
    std::vector<std::tuple<TableMetadata*, RID, Tuple>> update_records_{};
    std::vector<std::tuple<TableMetadata*, RID, Tuple>> delete_records_{};
};

} // namespace nexus
