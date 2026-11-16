#include <gtest/gtest.h>

#include <filesystem>

#include "ceebase/buffer/buffer_pool_manager.hpp"

namespace ceebase {
namespace {

class BufferPoolManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_bpm_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "test_bpm.nxdb";
        std::filesystem::remove(db_path_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
};

TEST_F(BufferPoolManagerTest, BasicAllocationAndEviction) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    // Create a small buffer pool of size 3 frames
    const size_t pool_size = 3;
    BufferPoolManager bpm(pool_size, &dm);

    page_id_t p0 = INVALID_PAGE_ID;
    page_id_t p1 = INVALID_PAGE_ID;
    page_id_t p2 = INVALID_PAGE_ID;

    auto res0 = bpm.NewPage(&p0);
    auto res1 = bpm.NewPage(&p1);
    auto res2 = bpm.NewPage(&p2);

    ASSERT_TRUE(res0.ok());
    ASSERT_TRUE(res1.ok());
    ASSERT_TRUE(res2.ok());

    // Write unique byte signature into each page
    (*res0)->GetData()[0] = 0x11;
    (*res1)->GetData()[0] = 0x22;
    (*res2)->GetData()[0] = 0x33;

    // Unpin p0 and p1 (marked dirty)
    ASSERT_TRUE(bpm.UnpinPage(p0, true).ok());
    ASSERT_TRUE(bpm.UnpinPage(p1, true).ok());
    // Keep p2 pinned
    EXPECT_EQ((*res2)->GetPinCount(), 1);

    // Allocate p3: Must evict p0 (LRU)
    page_id_t p3 = INVALID_PAGE_ID;
    auto res3 = bpm.NewPage(&p3);
    ASSERT_TRUE(res3.ok());
    (*res3)->GetData()[0] = 0x44;
    ASSERT_TRUE(bpm.UnpinPage(p3, true).ok());

    // p0 should have been flushed to disk during eviction.
    // Fetch p0 back into memory: must evict p1
    auto fetch_p0 = bpm.FetchPage(p0);
    ASSERT_TRUE(fetch_p0.ok());
    EXPECT_EQ((*fetch_p0)->GetData()[0], 0x11);
    ASSERT_TRUE(bpm.UnpinPage(p0, false).ok());

    // Unpin p2
    ASSERT_TRUE(bpm.UnpinPage(p2, true).ok());
}

TEST_F(BufferPoolManagerTest, BufferExhaustionHandling) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    const size_t pool_size = 2;
    BufferPoolManager bpm(pool_size, &dm);

    page_id_t p0, p1, p2;
    auto res0 = bpm.NewPage(&p0);
    auto res1 = bpm.NewPage(&p1);
    ASSERT_TRUE(res0.ok());
    ASSERT_TRUE(res1.ok());

    // Both frames in the pool are now pinned (pin_count = 1).
    // Requesting a third page must fail with BufferPoolExhausted error.
    auto res2 = bpm.NewPage(&p2);
    EXPECT_FALSE(res2.ok());
    EXPECT_EQ(res2.status().code(), StatusCode::BufferPoolExhausted);

    // Unpin one page, now NewPage should succeed
    ASSERT_TRUE(bpm.UnpinPage(p0, false).ok());
    res2 = bpm.NewPage(&p2);
    EXPECT_TRUE(res2.ok());

    ASSERT_TRUE(bpm.UnpinPage(p1, false).ok());
    ASSERT_TRUE(bpm.UnpinPage(p2, false).ok());
}

TEST_F(BufferPoolManagerTest, PageGuardRAIIUnpinning) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(2, &dm);
    page_id_t pid = INVALID_PAGE_ID;

    {
        auto page_res = bpm.NewPage(&pid);
        ASSERT_TRUE(page_res.ok());
        PageGuard guard(&bpm, *page_res);
        EXPECT_EQ(guard.Get()->GetPinCount(), 1);
        guard.SetDirty(true);
        // PageGuard goes out of scope here
    }

    // Guard destruction must have unpinned the page automatically
    auto fetch_res = bpm.FetchPage(pid);
    ASSERT_TRUE(fetch_res.ok());
    EXPECT_EQ((*fetch_res)->GetPinCount(), 1); // incremented from 0 back to 1
    ASSERT_TRUE(bpm.UnpinPage(pid, false).ok());
}

} // namespace
} // namespace ceebase
