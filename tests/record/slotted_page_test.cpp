#include <gtest/gtest.h>

#include <array>
#include <string>

#include "nexus/record/slotted_page.hpp"

namespace nexus {
namespace {

TEST(SlottedPageTest, InitAndBasicInsertGet) {
    std::array<uint8_t, PAGE_SIZE> page_buf{};
    SlottedPage page(page_buf.data());
    page.Init(1, INVALID_PAGE_ID, INVALID_PAGE_ID);

    EXPECT_EQ(page.GetPageId(), 1);
    EXPECT_EQ(page.GetSlotCount(), 0);
    EXPECT_TRUE(page.VerifyChecksum());

    std::string rec1 = "First Record in NexusDB";
    std::span<const uint8_t> span1(reinterpret_cast<const uint8_t*>(rec1.data()), rec1.size());

    auto slot1 = page.InsertRecord(span1);
    ASSERT_TRUE(slot1.ok());
    EXPECT_EQ(*slot1, 0);
    EXPECT_EQ(page.GetSlotCount(), 1);
    EXPECT_TRUE(page.VerifyChecksum());

    auto retrieved1 = page.GetRecord(*slot1);
    ASSERT_TRUE(retrieved1.ok());
    std::string str1(reinterpret_cast<const char*>(retrieved1->data()), retrieved1->size());
    EXPECT_EQ(str1, rec1);
}

TEST(SlottedPageTest, MultipleRecordsUpdateAndDelete) {
    std::array<uint8_t, PAGE_SIZE> page_buf{};
    SlottedPage page(page_buf.data());
    page.Init(2);

    std::string r0 = "Record Zero";
    std::string r1 = "Record One - longer string here";
    std::string r2 = "Record Two";

    auto s0 = page.InsertRecord(std::span(reinterpret_cast<const uint8_t*>(r0.data()), r0.size()));
    auto s1 = page.InsertRecord(std::span(reinterpret_cast<const uint8_t*>(r1.data()), r1.size()));
    auto s2 = page.InsertRecord(std::span(reinterpret_cast<const uint8_t*>(r2.data()), r2.size()));

    ASSERT_TRUE(s0.ok());
    ASSERT_TRUE(s1.ok());
    ASSERT_TRUE(s2.ok());
    EXPECT_EQ(page.GetSlotCount(), 3);

    // In-place update of s0
    std::string r0_new = "Record 0000";
    ASSERT_TRUE(page.UpdateRecord(*s0, std::span(reinterpret_cast<const uint8_t*>(r0_new.data()), r0_new.size())).ok());

    auto got0 = page.GetRecord(*s0);
    ASSERT_TRUE(got0.ok());
    EXPECT_EQ(std::string(reinterpret_cast<const char*>(got0->data()), got0->size()), r0_new);

    // Delete s1
    ASSERT_TRUE(page.DeleteRecord(*s1).ok());
    EXPECT_FALSE(page.GetRecord(*s1).ok());

    // Reuse slot: Next insert should reuse slot 1
    std::string r3 = "Reused Slot One";
    auto s3 = page.InsertRecord(std::span(reinterpret_cast<const uint8_t*>(r3.data()), r3.size()));
    ASSERT_TRUE(s3.ok());
    EXPECT_EQ(*s3, *s1); // Slot 1 was reused!
}

TEST(SlottedPageTest, Defragmentation) {
    std::array<uint8_t, PAGE_SIZE> page_buf{};
    SlottedPage page(page_buf.data());
    page.Init(5);

    // Insert 5 records
    std::vector<slot_id_t> slots;
    for (int i = 0; i < 5; ++i) {
        std::string s = "Payload data for record " + std::to_string(i);
        auto slot = page.InsertRecord(std::span(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
        ASSERT_TRUE(slot.ok());
        slots.push_back(*slot);
    }

    // Delete odd slots (1 and 3)
    ASSERT_TRUE(page.DeleteRecord(slots[1]).ok());
    ASSERT_TRUE(page.DeleteRecord(slots[3]).ok());

    const size_t free_before = page.GetFreeSpaceRemaining();
    page.Defragment();
    const size_t free_after = page.GetFreeSpaceRemaining();

    // Defragmentation should have compacted the page
    EXPECT_GE(free_after, free_before);

    // Remaining records (0, 2, 4) must still be fully intact
    for (int idx : {0, 2, 4}) {
        auto rec = page.GetRecord(slots[static_cast<size_t>(idx)]);
        ASSERT_TRUE(rec.ok());
        std::string expected = "Payload data for record " + std::to_string(idx);
        EXPECT_EQ(std::string(reinterpret_cast<const char*>(rec->data()), rec->size()), expected);
    }
}

} // namespace
} // namespace nexus
