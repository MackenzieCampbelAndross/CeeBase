#pragma once

#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <string_view>

#include "ceebase/common/config.hpp"

namespace ceebase {

// Core Database Identifiers
using page_id_t = uint32_t;
using frame_id_t = uint32_t;
using lsn_t = uint64_t;
using txn_id_t = uint64_t;
using slot_id_t = uint16_t;

inline constexpr page_id_t INVALID_PAGE_ID = std::numeric_limits<page_id_t>::max();
inline constexpr frame_id_t INVALID_FRAME_ID = std::numeric_limits<frame_id_t>::max();
inline constexpr lsn_t INVALID_LSN = 0;
inline constexpr txn_id_t INVALID_TXN_ID = 0;
inline constexpr slot_id_t INVALID_SLOT_ID = std::numeric_limits<slot_id_t>::max();

// Record Identifier (RID)
// Identifies a specific record location in a slotted page within a table heap.
struct RID {
    page_id_t page_id{INVALID_PAGE_ID};
    slot_id_t slot_id{INVALID_SLOT_ID};

    constexpr RID() = default;
    constexpr RID(page_id_t p_id, slot_id_t s_id) : page_id(p_id), slot_id(s_id) {}

    [[nodiscard]] constexpr bool IsValid() const noexcept {
        return page_id != INVALID_PAGE_ID && slot_id != INVALID_SLOT_ID;
    }

    [[nodiscard]] constexpr bool operator==(const RID& other) const noexcept {
        return page_id == other.page_id && slot_id == other.slot_id;
    }

    [[nodiscard]] constexpr bool operator!=(const RID& other) const noexcept {
        return !(*this == other);
    }

    [[nodiscard]] std::string ToString() const {
        return "[" + std::to_string(page_id) + ":" + std::to_string(slot_id) + "]";
    }
};

// Supported SQL / Engine Data Types
enum class DataType : uint8_t {
    INVALID = 0,
    INTEGER = 1,  // 32-bit signed integer
    BIGINT = 2,   // 64-bit signed integer
    FLOAT = 3,    // 32-bit IEEE 754 floating point
    DOUBLE = 4,   // 64-bit IEEE 754 double precision
    BOOLEAN = 5,  // 1-byte boolean (0 or 1)
    TEXT = 6      // Variable-length UTF-8 string
};

[[nodiscard]] constexpr std::string_view DataTypeToString(DataType type) noexcept {
    switch (type) {
        case DataType::INTEGER:
            return "INTEGER";
        case DataType::BIGINT:
            return "BIGINT";
        case DataType::FLOAT:
            return "FLOAT";
        case DataType::DOUBLE:
            return "DOUBLE";
        case DataType::BOOLEAN:
            return "BOOLEAN";
        case DataType::TEXT:
            return "TEXT";
        case DataType::INVALID:
        default:
            return "INVALID";
    }
}

[[nodiscard]] constexpr size_t GetDataTypeFixedSize(DataType type) noexcept {
    switch (type) {
        case DataType::BOOLEAN:
            return 1;
        case DataType::INTEGER:
            return 4;
        case DataType::BIGINT:
            return 8;
        case DataType::FLOAT:
            return 4;
        case DataType::DOUBLE:
            return 8;
        case DataType::TEXT:
        case DataType::INVALID:
        default:
            return 0; // Variable-length or invalid
    }
}

} // namespace ceebase

// Custom hash specialization for RID to support unordered containers
namespace std {
template <>
struct hash<ceebase::RID> {
    size_t operator()(const ceebase::RID& rid) const noexcept {
        const size_t h1 = std::hash<ceebase::page_id_t>{}(rid.page_id);
        const size_t h2 = std::hash<ceebase::slot_id_t>{}(rid.slot_id);
        // Boost-style hash combine
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};
} // namespace std
