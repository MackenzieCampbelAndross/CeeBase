#pragma once

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <string>

#include "ceebase/common/config.hpp"
#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/common/types.hpp"

namespace ceebase {

struct DatabaseHeader {
    static_assert(PAGE_SIZE == 4096, "PAGE_SIZE must be 4096");
    uint32_t magic{DB_FILE_MAGIC};
    uint16_t version_major{CEEBASE_VERSION_MAJOR};
    uint16_t version_minor{CEEBASE_VERSION_MINOR};
    uint32_t page_size{PAGE_SIZE};
    uint32_t total_pages{1};
    page_id_t first_free_page_id{INVALID_PAGE_ID};
    page_id_t catalog_root_page_id{INVALID_PAGE_ID};
    uint32_t checksum{0};
};

class DiskManager {
public:
    DiskManager();
    explicit DiskManager(std::filesystem::path db_file_path);
    ~DiskManager();

    // Disable copy semantics
    DiskManager(const DiskManager&) = delete;
    DiskManager& operator=(const DiskManager&) = delete;

    // Move semantics
    DiskManager(DiskManager&& other) noexcept;
    DiskManager& operator=(DiskManager&& other) noexcept;

    // Database File Operations
    Status CreateDatabase(const std::filesystem::path& file_path);
    Status OpenDatabase(const std::filesystem::path& file_path);
    void Close();

    [[nodiscard]] bool IsOpen() const noexcept;
    [[nodiscard]] const std::filesystem::path& GetFilePath() const noexcept { return file_path_; }
    [[nodiscard]] uint32_t GetNumPages() const noexcept { return num_pages_; }
    [[nodiscard]] const DatabaseHeader& GetHeader() const noexcept { return header_; }

    // Page Block I/O Operations (4096-byte fixed blocks)
    Status ReadPage(page_id_t page_id, uint8_t* page_data);
    Status WritePage(page_id_t page_id, const uint8_t* page_data);

    // Page Allocation and Deallocation
    Result<page_id_t> AllocatePage();
    Status DeallocatePage(page_id_t page_id);

    // Durability & Disk Synchronization
    Status Sync();

private:
    Status WriteHeaderPage();
    Status ReadAndValidateHeaderPage();
    void CloseInternal();

    std::filesystem::path file_path_{};
    FILE* db_file_{nullptr};
    uint32_t num_pages_{0};
    DatabaseHeader header_{};
    mutable std::mutex db_io_latch_{};
};

} // namespace ceebase
