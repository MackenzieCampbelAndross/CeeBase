#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "ceebase/common/types.hpp"

namespace ceebase {

class Column {
public:
    Column(std::string name, DataType type, bool is_nullable = true)
        : name_(std::move(name)), type_(type), is_nullable_(is_nullable), offset_(0) {}

    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }
    [[nodiscard]] DataType GetType() const noexcept { return type_; }
    [[nodiscard]] bool IsNullable() const noexcept { return is_nullable_; }
    [[nodiscard]] uint32_t GetOffset() const noexcept { return offset_; }
    [[nodiscard]] bool IsInlined() const noexcept { return type_ != DataType::TEXT; }

    [[nodiscard]] uint32_t GetStorageSize() const noexcept {
        if (IsInlined()) {
            return static_cast<uint32_t>(GetDataTypeFixedSize(type_));
        }
        // Variable-length column: stored as 2-byte offset + 2-byte length in fixed area
        return 4;
    }

    void SetOffset(uint32_t offset) noexcept { offset_ = offset; }

private:
    std::string name_;
    DataType type_{DataType::INVALID};
    bool is_nullable_{true};
    uint32_t offset_{0};
};

class Schema {
public:
    Schema() = default;
    explicit Schema(std::vector<Column> columns) : columns_(std::move(columns)) {
        // Null bitmap size in bytes
        null_bitmap_size_ = static_cast<uint32_t>((columns_.size() + 7) / 8);

        uint32_t current_offset = null_bitmap_size_;
        for (auto& col : columns_) {
            col.SetOffset(current_offset);
            current_offset += col.GetStorageSize();
        }
        fixed_length_ = current_offset;
    }

    [[nodiscard]] size_t GetColumnCount() const noexcept { return columns_.size(); }
    [[nodiscard]] const Column& GetColumn(size_t idx) const { return columns_.at(idx); }
    [[nodiscard]] const std::vector<Column>& GetColumns() const noexcept { return columns_; }
    [[nodiscard]] uint32_t GetFixedLength() const noexcept { return fixed_length_; }
    [[nodiscard]] uint32_t GetNullBitmapSize() const noexcept { return null_bitmap_size_; }

    [[nodiscard]] int GetColumnIndex(std::string_view name) const noexcept {
        for (size_t i = 0; i < columns_.size(); ++i) {
            if (columns_[i].GetName() == name) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

private:
    std::vector<Column> columns_{};
    uint32_t fixed_length_{0};
    uint32_t null_bitmap_size_{0};
};

} // namespace ceebase
