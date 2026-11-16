#include "nexus/recovery/log_manager.hpp"

namespace nexus {

LogManager::LogManager(std::filesystem::path wal_path) : wal_path_(std::move(wal_path)) {
    // Open in append + binary mode
    wal_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    if (!wal_file_.is_open()) {
        std::ofstream creator(wal_path_, std::ios::binary | std::ios::out);
        creator.close();
        wal_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
    }
}

LogManager::~LogManager() {
    (void)Flush();
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
}

lsn_t LogManager::AppendRecord(LogRecord* record) {
    std::lock_guard<std::mutex> guard(latch_);
    lsn_t assigned_lsn = next_lsn_++;
    record->SetLSN(assigned_lsn);

    // Temp buffer for record serialization
    std::vector<uint8_t> temp_buf(65536);
    ByteWriter writer(temp_buf);
    auto s = record->Serialize(writer);
    if (s.ok()) {
        size_t written = writer.BytesWritten();
        buffer_.insert(buffer_.end(), temp_buf.begin(), temp_buf.begin() + static_cast<ptrdiff_t>(written));
    }
    return assigned_lsn;
}

Status LogManager::Flush() {
    std::lock_guard<std::mutex> guard(latch_);
    if (buffer_.empty()) {
        return Status::Ok();
    }

    if (!wal_file_.is_open()) {
        wal_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
        if (!wal_file_.is_open()) {
            return Status::IoError("LogManager: Failed opening WAL file for flush");
        }
    }

    wal_file_.write(reinterpret_cast<const char*>(buffer_.data()), static_cast<std::streamsize>(buffer_.size()));
    wal_file_.flush();
    if (!wal_file_.good()) {
        return Status::IoError("LogManager: Failed writing WAL buffer to disk");
    }

    persisted_lsn_ = next_lsn_.load() - 1;
    buffer_.clear();
    return Status::Ok();
}

Result<std::vector<LogRecord>> LogManager::ReadAllRecords() {
    std::lock_guard<std::mutex> guard(latch_);
    if (wal_file_.is_open()) {
        wal_file_.flush();
        wal_file_.close();
    }

    std::ifstream reader(wal_path_, std::ios::binary);
    if (!reader.is_open()) {
        return Status::NotFound("LogManager: WAL file not found for reading");
    }

    std::vector<uint8_t> all_bytes((std::istreambuf_iterator<char>(reader)),
                                    std::istreambuf_iterator<char>());
    reader.close();

    // Reopen wal_file_ for future appends
    wal_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);

    std::vector<LogRecord> records;
    if (all_bytes.empty()) {
        return records;
    }

    ByteReader byte_reader(all_bytes);
    while (byte_reader.Remaining() > 0) {
        auto rec_res = LogRecord::Deserialize(byte_reader);
        if (!rec_res.ok()) {
            break;
        }
        if (rec_res->GetLSN() >= next_lsn_.load()) {
            next_lsn_ = rec_res->GetLSN() + 1;
        }
        records.push_back(std::move(*rec_res));
    }

    return records;
}

void LogManager::Clear() {
    std::lock_guard<std::mutex> guard(latch_);
    if (wal_file_.is_open()) {
        wal_file_.close();
    }
    std::filesystem::remove(wal_path_);
    buffer_.clear();
    next_lsn_ = 1;
    persisted_lsn_ = 0;
    wal_file_.open(wal_path_, std::ios::binary | std::ios::in | std::ios::out | std::ios::app);
}

} // namespace nexus
