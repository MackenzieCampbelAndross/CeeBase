#include <gtest/gtest.h>

#include <array>
#include <vector>

#include "ceebase/common/byte_utils.hpp"

namespace ceebase {
namespace {

TEST(ByteUtilsTest, LowLevelEndianRoundTrip) {
    uint8_t buffer[8];

    // U16
    EncodeU16(0x1234, buffer);
    EXPECT_EQ(buffer[0], 0x34);
    EXPECT_EQ(buffer[1], 0x12);
    EXPECT_EQ(DecodeU16(buffer), 0x1234);

    // U32
    EncodeU32(0x12345678, buffer);
    EXPECT_EQ(buffer[0], 0x78);
    EXPECT_EQ(buffer[1], 0x56);
    EXPECT_EQ(buffer[2], 0x34);
    EXPECT_EQ(buffer[3], 0x12);
    EXPECT_EQ(DecodeU32(buffer), 0x12345678);

    // U64
    EncodeU64(0x0102030405060708ULL, buffer);
    EXPECT_EQ(buffer[0], 0x08);
    EXPECT_EQ(buffer[7], 0x01);
    EXPECT_EQ(DecodeU64(buffer), 0x0102030405060708ULL);

    // Negative Integers (Two's complement)
    EncodeI32(-12345, buffer);
    EXPECT_EQ(DecodeI32(buffer), -12345);

    EncodeI64(-9876543210LL, buffer);
    EXPECT_EQ(DecodeI64(buffer), -9876543210LL);

    // Floating point IEEE 754
    EncodeFloat(3.14159f, buffer);
    EXPECT_FLOAT_EQ(DecodeFloat(buffer), 3.14159f);

    EncodeDouble(2.718281828459, buffer);
    EXPECT_DOUBLE_EQ(DecodeDouble(buffer), 2.718281828459);
}

TEST(ByteUtilsTest, WriterAndReaderSequentialFlow) {
    std::array<uint8_t, 128> mem{};
    ByteWriter writer(mem);

    EXPECT_TRUE(writer.WriteU8(0xAB).ok());
    EXPECT_TRUE(writer.WriteU16(0xCDEF).ok());
    EXPECT_TRUE(writer.WriteU32(0x12345678).ok());
    EXPECT_TRUE(writer.WriteU64(0xAABBCCDDEEFF0011ULL).ok());
    EXPECT_TRUE(writer.WriteI32(-42).ok());
    EXPECT_TRUE(writer.WriteI64(-1000000000LL).ok());
    EXPECT_TRUE(writer.WriteFloat(1.5f).ok());
    EXPECT_TRUE(writer.WriteDouble(99.999).ok());
    EXPECT_TRUE(writer.WriteString("Hello CeeBase!").ok());

    const size_t written = writer.BytesWritten();
    EXPECT_GT(written, 0);

    ByteReader reader(std::span<const uint8_t>(mem.data(), written));

    auto u8_val = reader.ReadU8();
    ASSERT_TRUE(u8_val.ok());
    EXPECT_EQ(*u8_val, 0xAB);

    auto u16_val = reader.ReadU16();
    ASSERT_TRUE(u16_val.ok());
    EXPECT_EQ(*u16_val, 0xCDEF);

    auto u32_val = reader.ReadU32();
    ASSERT_TRUE(u32_val.ok());
    EXPECT_EQ(*u32_val, 0x12345678);

    auto u64_val = reader.ReadU64();
    ASSERT_TRUE(u64_val.ok());
    EXPECT_EQ(*u64_val, 0xAABBCCDDEEFF0011ULL);

    auto i32_val = reader.ReadI32();
    ASSERT_TRUE(i32_val.ok());
    EXPECT_EQ(*i32_val, -42);

    auto i64_val = reader.ReadI64();
    ASSERT_TRUE(i64_val.ok());
    EXPECT_EQ(*i64_val, -1000000000LL);

    auto f_val = reader.ReadFloat();
    ASSERT_TRUE(f_val.ok());
    EXPECT_FLOAT_EQ(*f_val, 1.5f);

    auto d_val = reader.ReadDouble();
    ASSERT_TRUE(d_val.ok());
    EXPECT_DOUBLE_EQ(*d_val, 99.999);

    auto str_val = reader.ReadString();
    ASSERT_TRUE(str_val.ok());
    EXPECT_EQ(*str_val, "Hello CeeBase!");

    EXPECT_EQ(reader.Remaining(), 0);
}

TEST(ByteUtilsTest, WriterCapacityExceeded) {
    std::array<uint8_t, 3> tiny_buf{};
    ByteWriter writer(tiny_buf);

    EXPECT_TRUE(writer.WriteU16(123).ok());
    EXPECT_EQ(writer.Remaining(), 1);

    // Attempting to write 4 bytes with only 1 remaining must fail cleanly
    Status s = writer.WriteU32(456);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(s.code(), StatusCode::OutOfRange);

    // Writing 1 byte succeeds
    EXPECT_TRUE(writer.WriteU8(7).ok());
    EXPECT_EQ(writer.Remaining(), 0);

    // Buffer fully full now
    EXPECT_FALSE(writer.WriteU8(9).ok());
}

TEST(ByteUtilsTest, ReaderBoundaryViolations) {
    std::array<uint8_t, 2> mem{0x01, 0x02};
    ByteReader reader(mem);

    auto u16_val = reader.ReadU16();
    ASSERT_TRUE(u16_val.ok());
    EXPECT_EQ(reader.Remaining(), 0);

    // Reading beyond buffer must return OutOfRange error, never crash
    auto u8_val = reader.ReadU8();
    EXPECT_FALSE(u8_val.ok());
    EXPECT_EQ(u8_val.status().code(), StatusCode::OutOfRange);
}

TEST(ByteUtilsTest, CRC32StandardVector) {
    // Standard IEEE 802.3 CRC-32 test vector: "123456789" -> 0xCBF43926
    const std::string test_data = "123456789";
    std::span<const uint8_t> span(reinterpret_cast<const uint8_t*>(test_data.data()), test_data.size());

    uint32_t checksum = ComputeCRC32(span);
    EXPECT_EQ(checksum, 0xCBF43926);

    // Test avalanche effect: single byte alteration must alter the checksum drastically
    std::string altered_data = "123456788"; // last char altered
    std::span<const uint8_t> altered_span(reinterpret_cast<const uint8_t*>(altered_data.data()), altered_data.size());
    uint32_t altered_checksum = ComputeCRC32(altered_span);

    EXPECT_NE(checksum, altered_checksum);
}

} // namespace
} // namespace ceebase
