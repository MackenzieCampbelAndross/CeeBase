#include <gtest/gtest.h>

#include "nexus/buffer/lru_replacer.hpp"

namespace nexus {
namespace {

TEST(LRUReplacerTest, BasicEvictionOrder) {
    LRUReplacer replacer(5);

    // Unpin frames 1, 2, 3 in sequence
    replacer.Unpin(1);
    replacer.Unpin(2);
    replacer.Unpin(3);
    EXPECT_EQ(replacer.Size(), 3);

    frame_id_t victim = INVALID_FRAME_ID;

    // First victim must be 1 (least recently used)
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);
    EXPECT_EQ(replacer.Size(), 2);

    // Pin 3 so it cannot be evicted
    replacer.Pin(3);
    EXPECT_EQ(replacer.Size(), 1);

    // Next victim must be 2
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);
    EXPECT_EQ(replacer.Size(), 0);

    // Replacer is now empty
    EXPECT_FALSE(replacer.Victim(&victim));
}

TEST(LRUReplacerTest, RepinningAndUnpinning) {
    LRUReplacer replacer(3);

    replacer.Unpin(1);
    replacer.Unpin(2);

    // Access 1 again: pin then unpin moves it to most recently used
    replacer.Pin(1);
    replacer.Unpin(1);

    frame_id_t victim = INVALID_FRAME_ID;
    // 2 should now be evicted before 1
    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 2);

    EXPECT_TRUE(replacer.Victim(&victim));
    EXPECT_EQ(victim, 1);
}

} // namespace
} // namespace nexus
