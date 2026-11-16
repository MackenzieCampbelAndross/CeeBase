#pragma once

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "nexus/common/result.hpp"
#include "nexus/common/status.hpp"

namespace nexus {

// Low-level Little-Endian Encoding Routines
// Deterministic, platform-independent bit manipulation.
inline void EncodeU8(uint8_t val, uint8_t* dst) noexcept {
    dst[0] = val;
}

inline void EncodeU16(uint16_t val, uint8_t* dst) noexcept {
    dst[0] = static_cast<uint8_t>(val & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
}

inline void EncodeU32(uint32_t val, uint8_t* dst) noexcept {
    dst[0] = static_cast<uint8_t>(val & 0xFF);
    dst[1] = static_cast<uint8_t>((val >> 8) & 0xFF);
    dst[2] = static_cast<uint8_t>((val >> 16) & 0xFF);
    dst[3] = static_cast<uint8_t>((val >> 24) & 0xFF);
}

inline void EncodeU64(uint64_t val, uint8_t* dst) noexcept {
    for (size_t i = 0; i < 8; ++i) {
        dst[i] = static_cast<uint8_t>((val >> (i * 8)) & 0xFF);
    }
}

inline void EncodeI32(int32_t val, uint8_t* dst) noexcept {
    EncodeU32(std::bit_cast<uint32_t>(val), dst);
}

inline void EncodeI64(int64_t val, uint8_t* dst) noexcept {
    EncodeU64(std::bit_cast<uint64_t>(val), dst);
}

inline void EncodeFloat(float val, uint8_t* dst) noexcept {
    EncodeU32(std::bit_cast<uint32_t>(val), dst);
}

inline void EncodeDouble(double val, uint8_t* dst) noexcept {
    EncodeU64(std::bit_cast<uint64_t>(val), dst);
}

// Low-level Little-Endian Decoding Routines
[[nodiscard]] inline uint8_t DecodeU8(const uint8_t* src) noexcept {
    return src[0];
}

[[nodiscard]] inline uint16_t DecodeU16(const uint8_t* src) noexcept {
    return static_cast<uint16_t>(src[0]) |
           (static_cast<uint16_t>(src[1]) << 8);
}

[[nodiscard]] inline uint32_t DecodeU32(const uint8_t* src) noexcept {
    return static_cast<uint32_t>(src[0]) |
           (static_cast<uint32_t>(src[1]) << 8) |
           (static_cast<uint32_t>(src[2]) << 16) |
           (static_cast<uint32_t>(src[3]) << 24);
}

[[nodiscard]] inline uint64_t DecodeU64(const uint8_t* src) noexcept {
    uint64_t val = 0;
    for (size_t i = 0; i < 8; ++i) {
        val |= (static_cast<uint64_t>(src[i]) << (i * 8));
    }
    return val;
}

[[nodiscard]] inline int32_t DecodeI32(const uint8_t* src) noexcept {
    return std::bit_cast<int32_t>(DecodeU32(src));
}

[[nodiscard]] inline int64_t DecodeI64(const uint8_t* src) noexcept {
    return std::bit_cast<int64_t>(DecodeU64(src));
}

[[nodiscard]] inline float DecodeFloat(const uint8_t* src) noexcept {
    return std::bit_cast<float>(DecodeU32(src));
}

[[nodiscard]] inline double DecodeDouble(const uint8_t* src) noexcept {
    return std::bit_cast<double>(DecodeU64(src));
}

// ByteWriter: Safe, checked sequential binary serialization into a bounded memory buffer
class ByteWriter {
public:
    explicit ByteWriter(std::span<uint8_t> buffer) : buffer_(buffer), offset_(0) {}
    ByteWriter(uint8_t* data, size_t size) : buffer_(data, size), offset_(0) {}

    [[nodiscard]] size_t Remaining() const noexcept { return buffer_.size() - offset_; }
    [[nodiscard]] size_t BytesWritten() const noexcept { return offset_; }
    [[nodiscard]] size_t Capacity() const noexcept { return buffer_.size(); }
    [[nodiscard]] uint8_t* CurrentPtr() noexcept { return buffer_.data() + offset_; }
    [[nodiscard]] const uint8_t* Data() const noexcept { return buffer_.data(); }

    Status Seek(size_t new_offset) {
        if (new_offset > buffer_.size()) {
            return Status::OutOfRange("Seek offset exceeds buffer capacity");
        }
        offset_ = new_offset;
        return Status::Ok();
    }

    Status WriteU8(uint8_t val);
    Status WriteU16(uint16_t val);
    Status WriteU32(uint32_t val);
    Status WriteU64(uint64_t val);
    Status WriteI32(int32_t val);
    Status WriteI64(int64_t val);
    Status WriteFloat(float val);
    Status WriteDouble(double val);
    Status WriteBytes(std::span<const uint8_t> bytes);
    Status WriteString(std::string_view str); // 2-byte length prefixed string

private:
    [[nodiscard]] bool HasCapacity(size_t bytes) const noexcept {
        return offset_ + bytes <= buffer_.size();
    }

    std::span<uint8_t> buffer_;
    size_t offset_{0};
};

// ByteReader: Safe, checked sequential binary deserialization from a bounded memory buffer
class ByteReader {
public:
    explicit ByteReader(std::span<const uint8_t> buffer) : buffer_(buffer), offset_(0) {}
    ByteReader(const uint8_t* data, size_t size) : buffer_(data, size), offset_(0) {}

    [[nodiscard]] size_t Remaining() const noexcept { return buffer_.size() - offset_; }
    [[nodiscard]] size_t BytesRead() const noexcept { return offset_; }
    [[nodiscard]] size_t TotalSize() const noexcept { return buffer_.size(); }
    [[nodiscard]] const uint8_t* CurrentPtr() const noexcept { return buffer_.data() + offset_; }

    Status Seek(size_t new_offset) {
        if (new_offset > buffer_.size()) {
            return Status::OutOfRange("Seek offset exceeds buffer boundary");
        }
        offset_ = new_offset;
        return Status::Ok();
    }

    Status Skip(size_t bytes) {
        if (Remaining() < bytes) {
            return Status::OutOfRange("Skip exceeds remaining buffer bytes");
        }
        offset_ += bytes;
        return Status::Ok();
    }

    Result<uint8_t> ReadU8();
    Result<uint16_t> ReadU16();
    Result<uint32_t> ReadU32();
    Result<uint64_t> ReadU64();
    Result<int32_t> ReadI32();
    Result<int64_t> ReadI64();
    Result<float> ReadFloat();
    Result<double> ReadDouble();
    Result<std::vector<uint8_t>> ReadBytes(size_t count);
    Result<std::string> ReadString(); // Reads 2-byte length prefixed string

private:
    [[nodiscard]] bool HasRemaining(size_t bytes) const noexcept {
        return offset_ + bytes <= buffer_.size();
    }

    std::span<const uint8_t> buffer_;
    size_t offset_{0};
};

// Cyclic Redundancy Check (CRC-32 IEEE 802.3 standard polynomial)
// Used for page and WAL record checksum integrity validation.
[[nodiscard]] uint32_t ComputeCRC32(std::span<const uint8_t> data, uint32_t initial = 0) noexcept;

} // namespace nexus
