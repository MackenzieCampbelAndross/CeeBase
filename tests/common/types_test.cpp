#include <gtest/gtest.h>

#include <unordered_set>

#include "nexus/common/config.hpp"
#include "nexus/common/types.hpp"

namespace nexus {
namespace {

TEST(TypesTest, CompileTimeConstants) {
    EXPECT_EQ(PAGE_SIZE, 4096);
    EXPECT_EQ(INVALID_PAGE_ID, 0xFFFFFFFF);
    EXPECT_EQ(INVALID_FRAME_ID, 0xFFFFFFFF);
    EXPECT_EQ(INVALID_LSN, 0);
    EXPECT_EQ(INVALID_TXN_ID, 0);
    EXPECT_EQ(INVALID_SLOT_ID, 0xFFFF);
}

TEST(RIDTest, DefaultInvalid) {
    RID rid;
    EXPECT_FALSE(rid.IsValid());
    EXPECT_EQ(rid.page_id, INVALID_PAGE_ID);
    EXPECT_EQ(rid.slot_id, INVALID_SLOT_ID);
}

TEST(RIDTest, ConstructionAndValidity) {
    RID rid(10, 3);
    EXPECT_TRUE(rid.IsValid());
    EXPECT_EQ(rid.page_id, 10);
    EXPECT_EQ(rid.slot_id, 3);
    EXPECT_EQ(rid.ToString(), "[10:3]");
}

TEST(RIDTest, EqualityAndHashing) {
    RID r1(10, 3);
    RID r2(10, 3);
    RID r3(10, 4);
    RID r4(11, 3);

    EXPECT_EQ(r1, r2);
    EXPECT_NE(r1, r3);
    EXPECT_NE(r1, r4);

    std::unordered_set<RID> rid_set;
    rid_set.insert(r1);
    rid_set.insert(r3);
    rid_set.insert(r4);

    EXPECT_EQ(rid_set.size(), 3);
    EXPECT_TRUE(rid_set.contains(r2)); // r2 is equal to r1
}

TEST(DataTypeTest, StringRepresentation) {
    EXPECT_EQ(DataTypeToString(DataType::INTEGER), "INTEGER");
    EXPECT_EQ(DataTypeToString(DataType::BIGINT), "BIGINT");
    EXPECT_EQ(DataTypeToString(DataType::FLOAT), "FLOAT");
    EXPECT_EQ(DataTypeToString(DataType::DOUBLE), "DOUBLE");
    EXPECT_EQ(DataTypeToString(DataType::BOOLEAN), "BOOLEAN");
    EXPECT_EQ(DataTypeToString(DataType::TEXT), "TEXT");
    EXPECT_EQ(DataTypeToString(DataType::INVALID), "INVALID");
}

TEST(DataTypeTest, FixedSizes) {
    EXPECT_EQ(GetDataTypeFixedSize(DataType::BOOLEAN), 1);
    EXPECT_EQ(GetDataTypeFixedSize(DataType::INTEGER), 4);
    EXPECT_EQ(GetDataTypeFixedSize(DataType::BIGINT), 8);
    EXPECT_EQ(GetDataTypeFixedSize(DataType::FLOAT), 4);
    EXPECT_EQ(GetDataTypeFixedSize(DataType::DOUBLE), 8);
    EXPECT_EQ(GetDataTypeFixedSize(DataType::TEXT), 0);
    EXPECT_EQ(GetDataTypeFixedSize(DataType::INVALID), 0);
}

} // namespace
} // namespace nexus
