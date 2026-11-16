#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <random>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/index/b_plus_tree.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace ceebase {
namespace {

class BPlusTreeTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_btree_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "btree.nxdb";
        std::filesystem::remove(db_path_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
};

TEST_F(BPlusTreeTest, BasicInsertAndPointLookup) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(10, &dm);
    // Configurable small capacity to force tree splits
    BPlusTreeIndex tree("test_idx", &bpm, INVALID_PAGE_ID, 4, 4);

    EXPECT_TRUE(tree.IsEmpty());

    ASSERT_TRUE(tree.Insert(10, RID(1, 0)).ok());
    ASSERT_TRUE(tree.Insert(20, RID(1, 1)).ok());
    ASSERT_TRUE(tree.Insert(5, RID(1, 2)).ok());

    EXPECT_FALSE(tree.IsEmpty());

    RID r;
    EXPECT_TRUE(tree.GetValue(10, &r));
    EXPECT_EQ(r, RID(1, 0));

    EXPECT_TRUE(tree.GetValue(20, &r));
    EXPECT_EQ(r, RID(1, 1));

    EXPECT_TRUE(tree.GetValue(5, &r));
    EXPECT_EQ(r, RID(1, 2));

    EXPECT_FALSE(tree.GetValue(99, &r));
}

TEST_F(BPlusTreeTest, RandomizedInsertDeleteAgainstReferenceMap) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(20, &dm);
    // Tiny capacities (3 entries max) heavily stress splits, merges, and redistributions
    BPlusTreeIndex tree("stress_idx", &bpm, INVALID_PAGE_ID, 3, 3);
    std::map<int64_t, RID> reference_map;

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int64_t> key_dist(1, 2000);

    const int num_ops = 300;
    std::vector<int64_t> inserted_keys;

    // 1. Insert phase
    for (int i = 0; i < num_ops; ++i) {
        int64_t key = key_dist(rng);
        RID rid(static_cast<page_id_t>(i / 10 + 1), static_cast<slot_id_t>(i % 10));

        ASSERT_TRUE(tree.Insert(key, rid).ok());
        reference_map[key] = rid;
        inserted_keys.push_back(key);
    }

    // 2. Lookup verification against reference map
    for (const auto& [k, expected_rid] : reference_map) {
        RID r;
        ASSERT_TRUE(tree.GetValue(k, &r)) << "Failed lookup for key " << k;
        EXPECT_EQ(r, expected_rid);
    }

    // 3. Sequential range scan verification via Iterator
    auto ref_it = reference_map.begin();
    for (auto tree_it = tree.Begin(); tree_it != tree.End(); ++tree_it) {
        ASSERT_NE(ref_it, reference_map.end());
        auto [k, rid] = *tree_it;
        EXPECT_EQ(k, ref_it->first);
        EXPECT_EQ(rid, ref_it->second);
        ++ref_it;
    }
    EXPECT_EQ(ref_it, reference_map.end());

    // 4. Randomized Deletion phase (delete half the keys)
    for (size_t i = 0; i < inserted_keys.size() / 2; ++i) {
        int64_t k = inserted_keys[i];
        if (reference_map.find(k) != reference_map.end()) {
            ASSERT_TRUE(tree.Remove(k).ok());
            reference_map.erase(k);
        }
    }

    // 5. Verify remaining keys still intact
    for (const auto& [k, expected_rid] : reference_map) {
        RID r;
        ASSERT_TRUE(tree.GetValue(k, &r)) << "Post-delete failed lookup for key " << k;
        EXPECT_EQ(r, expected_rid);
    }
}

TEST_F(BPlusTreeTest, PersistenceAcrossRestart) {
    page_id_t saved_root = INVALID_PAGE_ID;

    {
        DiskManager dm;
        ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());
        BufferPoolManager bpm(10, &dm);
        BPlusTreeIndex tree("persist_idx", &bpm, INVALID_PAGE_ID, 4, 4);

        for (int i = 1; i <= 50; ++i) {
            ASSERT_TRUE(tree.Insert(i * 10, RID(static_cast<page_id_t>(i), 0)).ok());
        }
        saved_root = tree.GetRootPageId();
        EXPECT_NE(saved_root, INVALID_PAGE_ID);
        (void)bpm.FlushAllPages();
    }

    // Reopen database with fresh managers
    {
        DiskManager dm;
        ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
        BufferPoolManager bpm(10, &dm);
        BPlusTreeIndex tree("persist_idx", &bpm, saved_root, 4, 4);

        for (int i = 1; i <= 50; ++i) {
            RID r;
            ASSERT_TRUE(tree.GetValue(i * 10, &r));
            EXPECT_EQ(r, RID(static_cast<page_id_t>(i), 0));
        }
    }
}

} // namespace
} // namespace ceebase
