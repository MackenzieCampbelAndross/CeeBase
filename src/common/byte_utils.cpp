#include "nexus/common/byte_utils.hpp"

#include <array>
#include <cstring>

namespace nexus {

namespace {

// Compile-time generation of the IEEE 802.3 CRC-32 lookup table
consteval auto GenerateCRC32Table() {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; ++i) {
        uint32_t crc = i;
        for (uint32_t j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
        }
        table[i] = crc;
    }
    return table;
}

inline constexpr auto kCRC32Table = GenerateCRC32Table();

} // namespace

// ByteWriter implementations
Status ByteWriter::WriteU8(uint8_t val) {
    if (!HasCapacity(1)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write uint8");
    }
    EncodeU8(val, buffer_.data() + offset_);
    offset_ += 1;
    return Status::Ok();
}

Status ByteWriter::WriteU16(uint16_t val) {
    if (!HasCapacity(2)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write uint16");
    }
    EncodeU16(val, buffer_.data() + offset_);
    offset_ += 2;
    return Status::Ok();
}

Status ByteWriter::WriteU32(uint32_t val) {
    if (!HasCapacity(4)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write uint32");
    }
    EncodeU32(val, buffer_.data() + offset_);
    offset_ += 4;
    return Status::Ok();
}

Status ByteWriter::WriteU64(uint64_t val) {
    if (!HasCapacity(8)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write uint64");
    }
    EncodeU64(val, buffer_.data() + offset_);
    offset_ += 8;
    return Status::Ok();
}

Status ByteWriter::WriteI32(int32_t val) {
    if (!HasCapacity(4)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write int32");
    }
    EncodeI32(val, buffer_.data() + offset_);
    offset_ += 4;
    return Status::Ok();
}

Status ByteWriter::WriteI64(int64_t val) {
    if (!HasCapacity(8)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write int64");
    }
    EncodeI64(val, buffer_.data() + offset_);
    offset_ += 8;
    return Status::Ok();
}

Status ByteWriter::WriteFloat(float val) {
    if (!HasCapacity(4)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write float");
    }
    EncodeFloat(val, buffer_.data() + offset_);
    offset_ += 4;
    return Status::Ok();
}

Status ByteWriter::WriteDouble(double val) {
    if (!HasCapacity(8)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write double");
    }
    EncodeDouble(val, buffer_.data() + offset_);
    offset_ += 8;
    return Status::Ok();
}

Status ByteWriter::WriteBytes(std::span<const uint8_t> bytes) {
    if (!HasCapacity(bytes.size())) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write bytes span");
    }
    std::memcpy(buffer_.data() + offset_, bytes.data(), bytes.size());
    offset_ += bytes.size();
    return Status::Ok();
}

Status ByteWriter::WriteString(std::string_view str) {
    if (str.size() > 65535) {
        return Status::InvalidArgument("ByteWriter: String length exceeds 16-bit prefix limit (65535 bytes)");
    }
    const uint16_t len = static_cast<uint16_t>(str.size());
    if (!HasCapacity(2 + len)) {
        return Status::OutOfRange("ByteWriter: Insufficient capacity to write prefixed string");
    }
    EncodeU16(len, buffer_.data() + offset_);
    offset_ += 2;
    std::memcpy(buffer_.data() + offset_, str.data(), len);
    offset_ += len;
    return Status::Ok();
}

// ByteReader implementations
Result<uint8_t> ByteReader::ReadU8() {
    if (!HasRemaining(1)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading uint8");
    }
    uint8_t val = DecodeU8(buffer_.data() + offset_);
    offset_ += 1;
    return val;
}

Result<uint16_t> ByteReader::ReadU16() {
    if (!HasRemaining(2)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading uint16");
    }
    uint16_t val = DecodeU16(buffer_.data() + offset_);
    offset_ += 2;
    return val;
}

Result<uint32_t> ByteReader::ReadU32() {
    if (!HasRemaining(4)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading uint32");
    }
    uint32_t val = DecodeU32(buffer_.data() + offset_);
    offset_ += 4;
    return val;
}

Result<uint64_t> ByteReader::ReadU64() {
    if (!HasRemaining(8)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading uint64");
    }
    uint64_t val = DecodeU64(buffer_.data() + offset_);
    offset_ += 8;
    return val;
}

Result<int32_t> ByteReader::ReadI32() {
    if (!HasRemaining(4)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading int32");
    }
    int32_t val = DecodeI32(buffer_.data() + offset_);
    offset_ += 4;
    return val;
}

Result<int64_t> ByteReader::ReadI64() {
    if (!HasRemaining(8)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading int64");
    }
    int64_t val = DecodeI64(buffer_.data() + offset_);
    offset_ += 8;
    return val;
}

Result<float> ByteReader::ReadFloat() {
    if (!HasRemaining(4)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading float");
    }
    float val = DecodeFloat(buffer_.data() + offset_);
    offset_ += 4;
    return val;
}

Result<double> ByteReader::ReadDouble() {
    if (!HasRemaining(8)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading double");
    }
    double val = DecodeDouble(buffer_.data() + offset_);
    offset_ += 8;
    return val;
}

Result<std::vector<uint8_t>> ByteReader::ReadBytes(size_t count) {
    if (!HasRemaining(count)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading bytes span");
    }
    std::vector<uint8_t> result(count);
    std::memcpy(result.data(), buffer_.data() + offset_, count);
    offset_ += count;
    return result;
}

Result<std::string> ByteReader::ReadString() {
    if (!HasRemaining(2)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading string length prefix");
    }
    const uint16_t len = DecodeU16(buffer_.data() + offset_);
    if (!HasRemaining(2 + len)) {
        return Status::OutOfRange("ByteReader: Unexpected end of buffer reading string payload");
    }
    offset_ += 2;
    std::string str(reinterpret_cast<const char*>(buffer_.data() + offset_), len);
    offset_ += len;
    return str;
}

uint32_t ComputeCRC32(std::span<const uint8_t> data, uint32_t initial) noexcept {
    uint32_t crc = ~initial;
    for (const uint8_t byte : data) {
        crc = (crc >> 8) ^ kCRC32Table[(crc ^ byte) & 0xFF];
    }
    return ~crc;
}

} // namespace nexus
