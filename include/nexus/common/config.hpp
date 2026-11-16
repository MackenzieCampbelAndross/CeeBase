#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexus {

// Version Information
inline constexpr uint32_t NEXUS_VERSION_MAJOR = 0;
inline constexpr uint32_t NEXUS_VERSION_MINOR = 1;
inline constexpr uint32_t NEXUS_VERSION_PATCH = 0;
inline constexpr std::string_view NEXUS_VERSION_STRING = "0.1.0-alpha";

// Storage Subsystem Configuration
// Standard 4 KB fixed page size
inline constexpr size_t PAGE_SIZE = 4096;
static_assert(PAGE_SIZE == 4096, "NexusDB storage requires PAGE_SIZE to be exactly 4096 bytes");

// Database Header Magic: "NXDB" in ASCII (0x4E, 0x58, 0x44, 0x42)
inline constexpr uint32_t DB_FILE_MAGIC = 0x4E584442;

// Storage layout constants
inline constexpr uint32_t DB_HEADER_PAGE_ID = 0;
inline constexpr uint32_t FIRST_DATA_PAGE_ID = 1;

// Buffer Pool Default Configuration
inline constexpr size_t DEFAULT_BUFFER_POOL_SIZE = 128; // 128 frames * 4096 B = 512 KB

// Record & Catalog Limits
inline constexpr size_t MAX_VARCHAR_LENGTH = 2048;
inline constexpr size_t MAX_IDENTIFIER_LENGTH = 64;

} // namespace nexus
