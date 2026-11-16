#include "nexus/storage/disk_manager.hpp"

#include <array>
#include <cstring>
#include <iostream>

#include "nexus/common/byte_utils.hpp"
#include "nexus/storage/page.hpp"

namespace nexus {

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
    db_io_ = std::move(other.db_io_);
    num_pages_ = other.num_pages_;
    header_ = other.header_;
    other.num_pages_ = 0;
}

DiskManager& DiskManager::operator=(DiskManager&& other) noexcept {
    if (this != &other) {
        std::scoped_lock lock(db_io_latch_, other.db_io_latch_);
        Close();
        file_path_ = std::move(other.file_path_);
        db_io_ = std::move(other.db_io_);
        num_pages_ = other.num_pages_;
        header_ = other.header_;
        other.num_pages_ = 0;
    }
    return *this;
}

bool DiskManager::IsOpen() const noexcept {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    return db_io_.is_open();
}

Status DiskManager::CreateDatabase(const std::filesystem::path& file_path) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_io_.is_open()) {
        return Status::InvalidArgument("DiskManager: Another database file is already open");
    }

    if (std::filesystem::exists(file_path)) {
        return Status::AlreadyExists("DiskManager: Database file already exists: " + file_path.string());
    }

    // Create file and close to ensure existence
    {
        std::ofstream creator(file_path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!creator.is_open()) {
            return Status::IoError("DiskManager: Failed to create database file: " + file_path.string());
        }
    }

    file_path_ = file_path;
    db_io_.open(file_path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        return Status::IoError("DiskManager: Failed to open newly created database file");
    }

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

    return Status::Ok();
}

Status DiskManager::OpenDatabase(const std::filesystem::path& file_path) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_io_.is_open()) {
        return Status::InvalidArgument("DiskManager: Another database file is already open");
    }

    if (!std::filesystem::exists(file_path)) {
        return Status::NotFound("DiskManager: Database file not found: " + file_path.string());
    }

    const auto file_size = std::filesystem::file_size(file_path);
    if (file_size < PAGE_SIZE || (file_size % PAGE_SIZE != 0)) {
        return Status::Corruption("DiskManager: File size is not a valid multiple of 4096-byte pages");
    }

    file_path_ = file_path;
    db_io_.open(file_path_, std::ios::binary | std::ios::in | std::ios::out);
    if (!db_io_.is_open()) {
        return Status::IoError("DiskManager: Failed to open database file: " + file_path.string());
    }

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
    if (db_io_.is_open()) {
        db_io_.flush();
        db_io_.close();
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

    db_io_.seekp(0, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(page_buf.data()), PAGE_SIZE);
    if (!db_io_.good()) {
        return Status::IoError("DiskManager: Failed to write database header page to disk");
    }
    db_io_.flush();
    return Status::Ok();
}

Status DiskManager::ReadAndValidateHeaderPage() {
    std::array<uint8_t, PAGE_SIZE> page_buf{};
    db_io_.seekg(0, std::ios::beg);
    db_io_.read(reinterpret_cast<char*>(page_buf.data()), PAGE_SIZE);

    if (db_io_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
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
    if (!db_io_.is_open()) {
        return Status::IoError("DiskManager: No database file is open for reading");
    }

    const std::streamoff offset = static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE);
    db_io_.seekg(offset, std::ios::beg);
    db_io_.read(reinterpret_cast<char*>(page_data), PAGE_SIZE);

    if (db_io_.gcount() != static_cast<std::streamsize>(PAGE_SIZE)) {
        return Status::IoError("DiskManager: Incomplete page read for page_id " + std::to_string(page_id));
    }

    return Status::Ok();
}

Status DiskManager::WritePage(page_id_t page_id, const uint8_t* page_data) {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (!db_io_.is_open()) {
        return Status::IoError("DiskManager: No database file is open for writing");
    }

    const std::streamoff offset = static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE);
    db_io_.seekp(offset, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(page_data), PAGE_SIZE);

    if (!db_io_.good()) {
        return Status::IoError("DiskManager: Failed writing page_id " + std::to_string(page_id));
    }

    return Status::Ok();
}

Result<page_id_t> DiskManager::AllocatePage() {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (!db_io_.is_open()) {
        return Status::IoError("DiskManager: Cannot allocate page; no file open");
    }

    // Reuse page from free list if available
    if (header_.first_free_page_id != INVALID_PAGE_ID) {
        page_id_t free_page_id = header_.first_free_page_id;

        // Read the next free page pointer stored in the free page's header (bytes 4..7)
        std::array<uint8_t, PAGE_SIZE> free_buf{};
        const std::streamoff offset = static_cast<std::streamoff>(free_page_id) * static_cast<std::streamoff>(PAGE_SIZE);
        db_io_.seekg(offset, std::ios::beg);
        db_io_.read(reinterpret_cast<char*>(free_buf.data()), PAGE_SIZE);

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

    const std::streamoff offset = static_cast<std::streamoff>(new_page_id) * static_cast<std::streamoff>(PAGE_SIZE);
    db_io_.seekp(offset, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(zero_page.data()), PAGE_SIZE);

    if (!db_io_.good()) {
        return Status::IoError("DiskManager: Failed appending new page to disk");
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
    if (!db_io_.is_open()) {
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

    const std::streamoff offset = static_cast<std::streamoff>(page_id) * static_cast<std::streamoff>(PAGE_SIZE);
    db_io_.seekp(offset, std::ios::beg);
    db_io_.write(reinterpret_cast<const char*>(free_buf.data()), PAGE_SIZE);

    if (!db_io_.good()) {
        return Status::IoError("DiskManager: Failed writing deallocated page to disk");
    }

    header_.first_free_page_id = page_id;
    return WriteHeaderPage();
}

Status DiskManager::Sync() {
    std::lock_guard<std::mutex> guard(db_io_latch_);
    if (db_io_.is_open()) {
        db_io_.flush();
    }
    return Status::Ok();
}

} // namespace nexus
