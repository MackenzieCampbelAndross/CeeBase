#include "nexus/storage/disk_manager.hpp"

#include <array>
#include <cstring>
#include <iostream>

#include "nexus/common/byte_utils.hpp"
#include "nexus/common/os_utils.hpp"
#include "nexus/storage/page.hpp"

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

DiskManager::DiskManager() = default;

DiskManager::DiskManager(std::filesystem::path db_file_path) {
    if (std::filesystem::exists(db_file_path)) {
        (void)OpenDatabase(db_file_path);
    } else {
        (void)CreateDatabase(db_file_path);
    }
}

DiskManager::~DiskManager() {
    Close();
}

DiskManager::DiskManager(DiskManager&& other) noexcept {
    std::lock_guard<std::mutex> guard(other.db_io_latch_);
    file_path_ = std::move(other.file_path_);
    db_file_ = other.db_file_;
    other.db_file_ = nullptr;
    num_pages_ = other.num_pages_;
    header_ = other.header_;
    other.num_pages_ = 0;
}

DiskManager& DiskManager::operator=(DiskManager&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(db_io_latch_, other.db_io_latch_);
        Close();
        file_path_ = std::move(other.file_path_);
        db_file_ = other.db_file_;
        other.db_file_ = nullptr;
        num_pages_ = other.num_pages_;
        header_ = other.header_;
        other.num_pages_ = 0;
    }
    return *this;
}

bool DiskManager::IsOpen() const noexcept {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    return db_file_ != nullptr;
}

Status DiskManager::CreateDatabase(const std::filesystem::path& file_path) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ != nullptr) {
        return Status::InvalidArgument("DiskManager: Another database file is already open");
    }

    if (std::filesystem::exists(file_path)) {
        return Status::AlreadyExists("DiskManager: Database file already exists: " + file_path.string());
    }

    FILE* fp = OpenBinaryFile(file_path, "w+b");
    if (!fp) {
        return Status::IoError("DiskManager: Failed to create database file: " + file_path.string());
    }

    file_path_ = file_path;
    db_file_ = fp;

    // Initialize Database Header
    header_.magic = DB_FILE_MAGIC;
    header_.version_major = NEXUS_VERSION_MAJOR;
    header_.version_minor = NEXUS_VERSION_MINOR;
    header_.page_size = static_cast<uint32_t>(PAGE_SIZE);
    header_.total_pages = 1;
    header_.first_free_page_id = INVALID_PAGE_ID;
    header_.catalog_root_page_id = INVALID_PAGE_ID;
    num_pages_ = 1;

    // Write Page 0 (Header Page)
    Status s = WriteHeaderPage();
    if (!s.ok()) {
        CloseInternal();
        std::filesystem::remove(file_path_);
        return s;
    }

    return os::SyncFile(db_file_);
}

Status DiskManager::OpenDatabase(const std::filesystem::path& file_path) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ != nullptr) {
        return Status::InvalidArgument("DiskManager: Another database file is already open");
    }

    if (!std::filesystem::exists(file_path)) {
        return Status::NotFound("DiskManager: Database file not found: " + file_path.string());
    }

    const auto file_size = std::filesystem::file_size(file_path);
    if (file_size < PAGE_SIZE || (file_size % PAGE_SIZE != 0)) {
        return Status::Corruption("DiskManager: File size is not a valid multiple of 4096-byte pages");
    }

    FILE* fp = OpenBinaryFile(file_path, "r+b");
    if (!fp) {
        return Status::IoError("DiskManager: Failed to open database file: " + file_path.string());
    }

    file_path_ = file_path;
    db_file_ = fp;
    num_pages_ = static_cast<uint32_t>(file_size / PAGE_SIZE);

    Status s = ReadAndValidateHeaderPage();
    if (!s.ok()) {
        CloseInternal();
        return s;
    }

    return Status::Ok();
}

void DiskManager::Close() {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    CloseInternal();
}

void DiskManager::CloseInternal() {
    if (db_file_ != nullptr) {
        (void)os::SyncFile(db_file_);
        std::fclose(db_file_);
        db_file_ = nullptr;
    }
    num_pages_ = 0;
    file_path_.clear();
}

Status DiskManager::WriteHeaderPage() {
    std::array<uint8_t, PAGE_SIZE> page_buf{};
    ByteWriter writer(page_buf);

    // Page 0 Header fields
    auto s1 = writer.WriteU32(header_.magic);
    auto s2 = writer.WriteU16(header_.version_major);
    auto s3 = writer.WriteU16(header_.version_minor);
    auto s4 = writer.WriteU32(header_.page_size);
    auto s5 = writer.WriteU32(header_.total_pages);
    auto s6 = writer.WriteU32(header_.first_free_page_id);
    auto s7 = writer.WriteU32(header_.catalog_root_page_id);
    if (!s1.ok() || !s2.ok() || !s3.ok() || !s4.ok() || !s5.ok() || !s6.ok() || !s7.ok()) {
        return Status::Internal("DiskManager: Failed serializing header fields");
    }

    // Compute CRC32 checksum over the first 4092 bytes
    const uint32_t checksum = ComputeCRC32(std::span<const uint8_t>(page_buf.data(), PAGE_SIZE - 4));
    header_.checksum = checksum;

    // Write Checksum at the end of the page (bytes 4092-4095)
    EncodeU32(checksum, page_buf.data() + (PAGE_SIZE - 4));

    if (os::Seek64(db_file_, 0, SEEK_SET) != 0) {
        return Status::IoError("DiskManager: Failed seeking to header page");
    }

    size_t written = std::fwrite(page_buf.data(), 1, PAGE_SIZE, db_file_);
    if (written != PAGE_SIZE) {
        return Status::IoError("DiskManager: Failed to write database header page to disk");
    }
    if (std::fflush(db_file_) != 0) {
        return Status::IoError("DiskManager: Failed to flush header page to disk");
    }
    return Status::Ok();
}

Status DiskManager::ReadAndValidateHeaderPage() {
    std::array<uint8_t, PAGE_SIZE> page_buf{};
    if (os::Seek64(db_file_, 0, SEEK_SET) != 0) {
        return Status::IoError("DiskManager: Failed seeking to header page for read");
    }

    size_t read_bytes = std::fread(page_buf.data(), 1, PAGE_SIZE, db_file_);
    if (read_bytes != PAGE_SIZE) {
        return Status::IoError("DiskManager: Failed to read 4096-byte header page");
    }

    // Verify CRC-32 Checksum
    const uint32_t expected_checksum = DecodeU32(page_buf.data() + (PAGE_SIZE - 4));
    const uint32_t actual_checksum = ComputeCRC32(std::span<const uint8_t>(page_buf.data(), PAGE_SIZE - 4));
    if (expected_checksum != actual_checksum) {
        return Status::Corruption("DiskManager: Header page CRC-32 checksum mismatch (expected " +
                                  std::to_string(expected_checksum) + ", got " + std::to_string(actual_checksum) + ")");
    }

    ByteReader reader(page_buf);
    auto magic = reader.ReadU32();
    auto v_major = reader.ReadU16();
    auto v_minor = reader.ReadU16();
    auto p_size = reader.ReadU32();
    auto t_pages = reader.ReadU32();
    auto free_page = reader.ReadU32();
    auto catalog_page = reader.ReadU32();

    if (!magic.ok() || !v_major.ok() || !v_minor.ok() || !p_size.ok() || !t_pages.ok() ||
        !free_page.ok() || !catalog_page.ok()) {
        return Status::Corruption("DiskManager: Corrupted header page data layout");
    }

    if (*magic != DB_FILE_MAGIC) {
        return Status::Corruption("DiskManager: Invalid database file magic identifier");
    }

    if (*p_size != PAGE_SIZE) {
        return Status::Corruption("DiskManager: Incompatible page size: " + std::to_string(*p_size));
    }

    header_.magic = *magic;
    header_.version_major = *v_major;
    header_.version_minor = *v_minor;
    header_.page_size = *p_size;
    header_.total_pages = *t_pages;
    header_.first_free_page_id = *free_page;
    header_.catalog_root_page_id = *catalog_page;
    header_.checksum = expected_checksum;

    return Status::Ok();
}

Status DiskManager::ReadPage(page_id_t page_id, uint8_t* page_data) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ == nullptr) {
        return Status::IoError("DiskManager: No database file is open for reading");
    }

    const int64_t offset = static_cast<int64_t>(page_id) * static_cast<int64_t>(PAGE_SIZE);
    if (os::Seek64(db_file_, offset, SEEK_SET) != 0) {
        return Status::IoError("DiskManager: Failed seeking to page_id " + std::to_string(page_id));
    }

    size_t read_bytes = std::fread(page_data, 1, PAGE_SIZE, db_file_);
    if (read_bytes != PAGE_SIZE) {
        return Status::IoError("DiskManager: Incomplete page read for page_id " + std::to_string(page_id));
    }

    return Status::Ok();
}

Status DiskManager::WritePage(page_id_t page_id, const uint8_t* page_data) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ == nullptr) {
        return Status::IoError("DiskManager: No database file is open for writing");
    }

    const int64_t offset = static_cast<int64_t>(page_id) * static_cast<int64_t>(PAGE_SIZE);
    if (os::Seek64(db_file_, offset, SEEK_SET) != 0) {
        return Status::IoError("DiskManager: Failed seeking to write page_id " + std::to_string(page_id));
    }

    size_t written = std::fwrite(page_data, 1, PAGE_SIZE, db_file_);
    if (written != PAGE_SIZE) {
        return Status::IoError("DiskManager: Failed writing page_id " + std::to_string(page_id));
    }

    if (std::fflush(db_file_) != 0) {
        return Status::IoError("DiskManager: Failed flushing page_id " + std::to_string(page_id));
    }

    return Status::Ok();
}

Result<page_id_t> DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ == nullptr) {
        return Status::IoError("DiskManager: Cannot allocate page; no file open");
    }

    // Reuse page from free list if available
    if (header_.first_free_page_id != INVALID_PAGE_ID) {
        page_id_t free_page_id = header_.first_free_page_id;

        // Read the next free page pointer stored in the free page's header (bytes 4..7)
        std::array<uint8_t, PAGE_SIZE> free_buf{};
        const int64_t offset = static_cast<int64_t>(free_page_id) * static_cast<int64_t>(PAGE_SIZE);
        if (os::Seek64(db_file_, offset, SEEK_SET) != 0) {
            return Status::IoError("DiskManager: Failed seeking free page");
        }
        size_t read_bytes = std::fread(free_buf.data(), 1, PAGE_SIZE, db_file_);
        if (read_bytes != PAGE_SIZE) {
            return Status::IoError("DiskManager: Failed reading free page");
        }

        page_id_t next_free_id = DecodeU32(free_buf.data() + 4);
        header_.first_free_page_id = next_free_id;

        Status s = WriteHeaderPage();
        if (!s.ok()) {
            return s;
        }

        return free_page_id;
    }

    // Append new 4096-byte page to the end of the file
    page_id_t new_page_id = num_pages_;
    std::array<uint8_t, PAGE_SIZE> zero_page{};
    zero_page.fill(0);

    const int64_t offset = static_cast<int64_t>(new_page_id) * static_cast<int64_t>(PAGE_SIZE);
    if (os::Seek64(db_file_, offset, SEEK_SET) != 0) {
        return Status::IoError("DiskManager: Failed seeking to append page");
    }
    size_t written = std::fwrite(zero_page.data(), 1, PAGE_SIZE, db_file_);
    if (written != PAGE_SIZE) {
        return Status::IoError("DiskManager: Failed appending new page to disk");
    }
    if (std::fflush(db_file_) != 0) {
        return Status::IoError("DiskManager: Failed flushing new page to disk");
    }

    num_pages_++;
    header_.total_pages = num_pages_;

    Status s = WriteHeaderPage();
    if (!s.ok()) {
        return s;
    }

    return new_page_id;
}

Status DiskManager::DeallocatePage(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ == nullptr) {
        return Status::IoError("DiskManager: No database file is open");
    }

    if (page_id == DB_HEADER_PAGE_ID || page_id >= num_pages_) {
        return Status::InvalidArgument("DiskManager: Invalid page ID to deallocate: " + std::to_string(page_id));
    }

    // Format page as a free page, pointing next to current first_free_page_id
    std::array<uint8_t, PAGE_SIZE> free_buf{};
    free_buf.fill(0);
    EncodeU32(static_cast<uint32_t>(PageType::FREE_PAGE), free_buf.data());
    EncodeU32(header_.first_free_page_id, free_buf.data() + 4);

    const int64_t offset = static_cast<int64_t>(page_id) * static_cast<int64_t>(PAGE_SIZE);
    if (os::Seek64(db_file_, offset, SEEK_SET) != 0) {
        return Status::IoError("DiskManager: Failed seeking to deallocate page");
    }
    size_t written = std::fwrite(free_buf.data(), 1, PAGE_SIZE, db_file_);
    if (written != PAGE_SIZE) {
        return Status::IoError("DiskManager: Failed writing deallocated page to disk");
    }
    if (std::fflush(db_file_) != 0) {
        return Status::IoError("DiskManager: Failed flushing deallocated page to disk");
    }

    header_.first_free_page_id = page_id;
    return WriteHeaderPage();
}

Status DiskManager::Sync() {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_file_ != nullptr) {
        return os::SyncFile(db_file_);
    }
    return Status::Ok();
}

} // namespace nexus
