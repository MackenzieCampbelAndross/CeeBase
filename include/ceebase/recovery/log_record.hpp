#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ceebase/common/byte_utils.hpp"
#include "ceebase/common/types.hpp"

namespace ceebase {

enum class LogRecordType : uint32_t {
    UNKNOWN = 0,
    BEGIN = 1,
    COMMIT = 2,
    ABORT = 3,
    INSERT = 4,
    UPDATE = 5,
    DELETE_REC = 6,
    CHECKPOINT = 7
};

[[nodiscard]] constexpr std::string_view LogRecordTypeToString(LogRecordType type) noexcept {
    switch (type) {
        case LogRecordType::BEGIN: return "BEGIN";
        case LogRecordType::COMMIT: return "COMMIT";
        case LogRecordType::ABORT: return "ABORT";
        case LogRecordType::INSERT: return "INSERT";
        case LogRecordType::UPDATE: return "UPDATE";
        case LogRecordType::DELETE_REC: return "DELETE";
        case LogRecordType::CHECKPOINT: return "CHECKPOINT";
        default: return "UNKNOWN";
    }
}

class LogRecord {
public:
    LogRecord() = default;

    // Txn lifecycle constructor (BEGIN, COMMIT, ABORT, CHECKPOINT)
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type)
        : prev_lsn_(prev_lsn), txn_id_(txn_id), type_(type) {}

    // Generalized data modification constructor
    LogRecord(txn_id_t txn_id, lsn_t prev_lsn, LogRecordType type, std::string table_name,
              RID rid, std::vector<uint8_t> before_image, std::vector<uint8_t> after_image)
        : prev_lsn_(prev_lsn), txn_id_(txn_id), type_(type),
          table_name_(std::move(table_name)), rid_(rid),
          before_image_(std::move(before_image)), after_image_(std::move(after_image)) {}

    // Helper factories
    static LogRecord MakeInsert(txn_id_t txn_id, lsn_t prev_lsn, std::string table_name,
                                RID rid, std::vector<uint8_t> after_image) {
        return LogRecord(txn_id, prev_lsn, LogRecordType::INSERT, std::move(table_name), rid, {}, std::move(after_image));
    }

    static LogRecord MakeUpdate(txn_id_t txn_id, lsn_t prev_lsn, std::string table_name,
                                RID rid, std::vector<uint8_t> before_image, std::vector<uint8_t> after_image) {
        return LogRecord(txn_id, prev_lsn, LogRecordType::UPDATE, std::move(table_name), rid, std::move(before_image), std::move(after_image));
    }

    static LogRecord MakeDelete(txn_id_t txn_id, lsn_t prev_lsn, std::string table_name,
                                RID rid, std::vector<uint8_t> before_image) {
        return LogRecord(txn_id, prev_lsn, LogRecordType::DELETE_REC, std::move(table_name), rid, std::move(before_image), {});
    }

    [[nodiscard]] lsn_t GetLSN() const noexcept { return lsn_; }
    void SetLSN(lsn_t lsn) noexcept { lsn_ = lsn; }

    [[nodiscard]] lsn_t GetPrevLSN() const noexcept { return prev_lsn_; }
    [[nodiscard]] txn_id_t GetTxnId() const noexcept { return txn_id_; }
    [[nodiscard]] LogRecordType GetType() const noexcept { return type_; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] RID GetRID() const noexcept { return rid_; }
    [[nodiscard]] const std::vector<uint8_t>& GetBeforeImage() const noexcept { return before_image_; }
    [[nodiscard]] const std::vector<uint8_t>& GetAfterImage() const noexcept { return after_image_; }

    // Serialization
    Status Serialize(ByteWriter& writer) const {
        auto s1 = writer.WriteU64(lsn_);
        auto s2 = writer.WriteU64(prev_lsn_);
        auto s3 = writer.WriteU64(txn_id_);
        auto s4 = writer.WriteU32(static_cast<uint32_t>(type_));
        auto s5 = writer.WriteString(table_name_);
        auto s6 = writer.WriteU32(rid_.page_id);
        auto s7 = writer.WriteU16(rid_.slot_id);
        auto s8 = writer.WriteU32(static_cast<uint32_t>(before_image_.size()));
        auto s9 = writer.WriteBytes(before_image_);
        auto s10 = writer.WriteU32(static_cast<uint32_t>(after_image_.size()));
        auto s11 = writer.WriteBytes(after_image_);

        if (!s1.ok() || !s2.ok() || !s3.ok() || !s4.ok() || !s5.ok() || !s6.ok() ||
            !s7.ok() || !s8.ok() || !s9.ok() || !s10.ok() || !s11.ok()) {
            return Status::OutOfRange("LogRecord: Insufficient buffer to serialize log record");
        }
        return Status::Ok();
    }

    // Deserialization
    static Result<LogRecord> Deserialize(ByteReader& reader) {
        auto lsn_res = reader.ReadU64();
        auto prev_lsn_res = reader.ReadU64();
        auto txn_id_res = reader.ReadU64();
        auto type_res = reader.ReadU32();
        auto tbl_name_res = reader.ReadString();
        auto page_id_res = reader.ReadU32();
        auto slot_id_res = reader.ReadU16();
        auto before_len_res = reader.ReadU32();
        if (!lsn_res.ok() || !prev_lsn_res.ok() || !txn_id_res.ok() || !type_res.ok() ||
            !tbl_name_res.ok() || !page_id_res.ok() || !slot_id_res.ok() || !before_len_res.ok()) {
            return Status::Corruption("LogRecord: Failed reading log header fields");
        }

        auto before_res = reader.ReadBytes(*before_len_res);
        auto after_len_res = reader.ReadU32();
        if (!before_res.ok() || !after_len_res.ok()) {
            return Status::Corruption("LogRecord: Failed reading before image payload");
        }

        auto after_res = reader.ReadBytes(*after_len_res);
        if (!after_res.ok()) {
            return Status::Corruption("LogRecord: Failed reading after image payload");
        }

        LogRecord rec;
        rec.lsn_ = *lsn_res;
        rec.prev_lsn_ = *prev_lsn_res;
        rec.txn_id_ = *txn_id_res;
        rec.type_ = static_cast<LogRecordType>(*type_res);
        rec.table_name_ = std::move(*tbl_name_res);
        rec.rid_ = RID(*page_id_res, *slot_id_res);
        rec.before_image_ = std::move(*before_res);
        rec.after_image_ = std::move(*after_res);

        return rec;
    }

private:
    lsn_t lsn_{INVALID_LSN};
    lsn_t prev_lsn_{INVALID_LSN};
    txn_id_t txn_id_{INVALID_TXN_ID};
    LogRecordType type_{LogRecordType::UNKNOWN};
    std::string table_name_{};
    RID rid_{};
    std::vector<uint8_t> before_image_{};
    std::vector<uint8_t> after_image_{};
};

} // namespace ceebase
