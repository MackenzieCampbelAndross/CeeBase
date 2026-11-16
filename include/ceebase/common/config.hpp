#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ceebase {

// Version Information
inline constexpr uint32_t CEEBASE_VERSION_MAJOR = 1;
inline constexpr uint32_t CEEBASE_VERSION_MINOR = 0;
inline constexpr uint32_t CEEBASE_VERSION_PATCH = 0;
inline constexpr std::string_view CEEBASE_VERSION_STRING = "1.0.0";

// Storage Subsystem Configuration
// Standard 4 KB fixed page size
inline constexpr size_t PAGE_SIZE = 4096;
static_assert(PAGE_SIZE == 4096, "CeeBase storage requires PAGE_SIZE to be exactly 4096 bytes");

// Database Header Magic: "CBDB" in ASCII (0x43, 0x42, 0x44, 0x42)
inline constexpr uint32_t DB_FILE_MAGIC = 0x43424442;
// Legacy Database Header Magic for backward compatibility: "NXDB"
inline constexpr uint32_t LEGACY_DB_FILE_MAGIC = 0x4E584442;

// Storage layout constants
inline constexpr uint32_t DB_HEADER_PAGE_ID = 0;
inline constexpr uint32_t FIRST_DATA_PAGE_ID = 1;

// Buffer Pool Default Configuration
inline constexpr size_t DEFAULT_BUFFER_POOL_SIZE = 128; // 128 frames * 4096 B = 512 KB

// Record & Catalog Limits
inline constexpr size_t MAX_VARCHAR_LENGTH = 2048;
inline constexpr size_t MAX_IDENTIFIER_LENGTH = 64;

} // namespace ceebase
