#include "nexus/recovery/log_manager.hpp"

#include <cstring>
#include <iostream>

#include "nexus/common/os_utils.hpp"

namespace nexus {

namespace {

FILE* OpenBinaryFile(const std::filesystem::path& path, const char* mode) {
#ifdef _WIN32
    std::wstring wmode(mode, mode + std::strlen(mode));
    return _wfopen(path.c_str(), wmode.c_str());
#else
    return std::fopen(path.c_str(), mode);
#endif
}

} // namespace

LogManager::LogManager(std::filesystem::path wal_path) : wal_path_(std::move(wal_path)) {
    // Open in append+read binary mode. If it does not exist, create it.
    wal_file_ = OpenBinaryFile(wal_path_, "a+b");
    if (!wal_file_) {
        wal_file_ = OpenBinaryFile(wal_path_, "w+b");
    }
}

LogManager::~LogManager() {
    (void)Flush();
    if (wal_file_ != nullptr) {
        std::fclose(wal_file_);
        wal_file_ = nullptr;
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

    if (!wal_file_) {
        wal_file_ = OpenBinaryFile(wal_path_, "a+b");
        if (!wal_file_) {
            return Status::IoError("LogManager: Failed opening WAL file for flush");
        }
    }

    size_t written = std::fwrite(buffer_.data(), 1, buffer_.size(), wal_file_);
    if (written != buffer_.size()) {
        return Status::IoError("LogManager: Failed writing WAL buffer to disk");
    }

    if (std::fflush(wal_file_) != 0) {
        return Status::IoError("LogManager: Failed to fflush WAL file");
    }

    // Force operating-system level durable disk sync (FlushFileBuffers on Windows, fsync on POSIX)
    Status sync_status = os::SyncFile(wal_file_);
    if (!sync_status.ok()) {
        return sync_status;
    }

    persisted_lsn_ = next_lsn_.load() - 1;
    buffer_.clear();
    return Status::Ok();
}

Result<std::vector<LogRecord>> LogManager::ReadAllRecords() {
    std::lock_guard<std::mutex> guard(latch_);
    if (wal_file_ != nullptr) {
        std::fflush(wal_file_);
        (void)os::SyncFile(wal_file_);
        std::fclose(wal_file_);
        wal_file_ = nullptr;
    }

    FILE* reader = OpenBinaryFile(wal_path_, "rb");
    if (!reader) {
        return Status::NotFound("LogManager: WAL file not found for reading");
    }

    os::Seek64(reader, 0, SEEK_END);
    int64_t file_len = os::Tell64(reader);
    os::Seek64(reader, 0, SEEK_SET);

    std::vector<uint8_t> all_bytes;
    if (file_len > 0) {
        all_bytes.resize(static_cast<size_t>(file_len));
        size_t read_bytes = std::fread(all_bytes.data(), 1, all_bytes.size(), reader);
        all_bytes.resize(read_bytes);
    }
    std::fclose(reader);

    // Reopen wal_file_ for future appends
    wal_file_ = OpenBinaryFile(wal_path_, "a+b");

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
    if (wal_file_ != nullptr) {
        std::fclose(wal_file_);
        wal_file_ = nullptr;
    }
    std::error_code ec;
    std::filesystem::remove(wal_path_, ec);
    buffer_.clear();
    next_lsn_ = 1;
    persisted_lsn_ = 0;
    wal_file_ = OpenBinaryFile(wal_path_, "w+b");
}

} // namespace nexus
