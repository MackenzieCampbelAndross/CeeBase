#include "nexus/record/tuple.hpp"

#include <cstring>
#include <sstream>
#include <stdexcept>

#include "nexus/common/byte_utils.hpp"

namespace nexus {

Tuple::Tuple(std::span<const uint8_t> data, RID rid)
    : data_(data.begin(), data.end()), rid_(rid) {}

Tuple::Tuple(const std::vector<Value>& values, const Schema& schema) {
    if (values.size() != schema.GetColumnCount()) {
        throw std::invalid_argument("Tuple: Values count does not match schema column count");
    }

    size_t total_size = schema.GetFixedLength();
    for (size_t i = 0; i < values.size(); ++i) {
        const auto& col = schema.GetColumn(i);
        if (!values[i].IsNull() && !col.IsInlined()) {
            total_size += values[i].GetText().size();
        }
    }

    data_.resize(total_size, 0);

    // 1. Populate Null Bitmap
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].IsNull()) {
            data_[i / 8] |= static_cast<uint8_t>(1 << (i % 8));
        }
    }

    // 2. Populate Inlined Values and Variable-length Pointers
    uint32_t current_var_offset = schema.GetFixedLength();
    for (size_t i = 0; i < values.size(); ++i) {
        if (values[i].IsNull()) {
            continue;
        }

        const auto& col = schema.GetColumn(i);
        uint8_t* slot_ptr = data_.data() + col.GetOffset();

        if (col.IsInlined()) {
            switch (col.GetType()) {
                case DataType::BOOLEAN:
                    EncodeU8(values[i].GetBoolean() ? 1 : 0, slot_ptr);
                    break;
                case DataType::INTEGER:
                    EncodeI32(values[i].GetInteger(), slot_ptr);
                    break;
                case DataType::BIGINT:
                    EncodeI64(values[i].GetBigInt(), slot_ptr);
                    break;
                case DataType::FLOAT:
                    EncodeFloat(values[i].GetFloat(), slot_ptr);
                    break;
                case DataType::DOUBLE:
                    EncodeDouble(values[i].GetDouble(), slot_ptr);
                    break;
                default:
                    throw std::runtime_error("Tuple: Unsupported inlined type in serialization");
            }
        } else {
            // Variable length (TEXT)
            const auto& text = values[i].GetText();
            const uint16_t str_len = static_cast<uint16_t>(text.size());
            const uint16_t offset_u16 = static_cast<uint16_t>(current_var_offset);

            EncodeU16(offset_u16, slot_ptr);
            EncodeU16(str_len, slot_ptr + 2);

            std::memcpy(data_.data() + current_var_offset, text.data(), str_len);
            current_var_offset += str_len;
        }
    }
}

bool Tuple::IsNull(const Schema& schema, size_t col_idx) const noexcept {
    if (data_.empty() || col_idx >= schema.GetColumnCount()) {
        return true;
    }
    const uint8_t byte = data_[col_idx / 8];
    return (byte & (1 << (col_idx % 8))) != 0;
}

Value Tuple::GetValue(const Schema& schema, size_t col_idx) const {
    const auto& col = schema.GetColumn(col_idx);
    if (IsNull(schema, col_idx)) {
        return Value::MakeNull(col.GetType());
    }

    const uint8_t* slot_ptr = data_.data() + col.GetOffset();
    if (col.IsInlined()) {
        switch (col.GetType()) {
            case DataType::BOOLEAN:
                return Value(DecodeU8(slot_ptr) != 0);
            case DataType::INTEGER:
                return Value(DecodeI32(slot_ptr));
            case DataType::BIGINT:
                return Value(DecodeI64(slot_ptr));
            case DataType::FLOAT:
                return Value(DecodeFloat(slot_ptr));
            case DataType::DOUBLE:
                return Value(DecodeDouble(slot_ptr));
            default:
                throw std::runtime_error("Tuple: Unsupported inlined type in deserialization");
        }
    }

    // Variable length (TEXT)
    const uint16_t offset = DecodeU16(slot_ptr);
    const uint16_t length = DecodeU16(slot_ptr + 2);

    if (offset + length > data_.size()) {
        throw std::runtime_error("Tuple: Corrupted variable length text bounds");
    }

    std::string text(reinterpret_cast<const char*>(data_.data() + offset), length);
    return Value(std::move(text));
}

std::string Tuple::ToString(const Schema& schema) const {
    std::ostringstream ss;
    ss << "(";
    for (size_t i = 0; i < schema.GetColumnCount(); ++i) {
        if (i > 0) {
            ss << ", ";
        }
        Value val = GetValue(schema, i);
        if (val.GetType() == DataType::TEXT && !val.IsNull()) {
            ss << "'" << val.ToString() << "'";
        } else {
            ss << val.ToString();
        }
    }
    ss << ")";
    return ss.str();
}

} // namespace nexus
