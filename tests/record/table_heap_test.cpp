#include <gtest/gtest.h>

#include <filesystem>
#include <vector>

#include "nexus/buffer/buffer_pool_manager.hpp"
#include "nexus/record/schema.hpp"
#include "nexus/record/table_heap.hpp"
#include "nexus/storage/disk_manager.hpp"

namespace nexus {
namespace {

class TableHeapTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "nexus_table_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "test_table.nxdb";
        std::filesystem::remove(db_path_);

        std::vector<Column> cols;
        cols.emplace_back("id", DataType::INTEGER, false);
        cols.emplace_back("name", DataType::TEXT, false);
        cols.emplace_back("val", DataType::DOUBLE, false);
        schema_ = std::make_unique<Schema>(std::move(cols));
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
    std::unique_ptr<Schema> schema_;
};

TEST_F(TableHeapTest, MultiPageTupleInsertAndIterate) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(5, &dm);
    auto heap_res = TableHeap::Create(&bpm);
    ASSERT_TRUE(heap_res.ok());
    auto heap = std::move(*heap_res);

    std::vector<RID> inserted_rids;
    const int num_tuples = 100;

    // Insert 100 tuples (which will span multiple slotted pages)
    for (int i = 0; i < num_tuples; ++i) {
        std::vector<Value> vals;
        vals.emplace_back(i);
        vals.emplace_back("User_" + std::to_string(i) + "_description_padding_string");
        vals.emplace_back(static_cast<double>(i) * 1.5);

        Tuple t(vals, *schema_);
        RID rid;
        ASSERT_TRUE(heap->InsertTuple(t, &rid).ok());
        EXPECT_TRUE(rid.IsValid());
        inserted_rids.push_back(rid);
    }

    // Verify each tuple can be retrieved individually by RID
    for (int i = 0; i < num_tuples; ++i) {
        auto fetched = heap->GetTuple(inserted_rids[static_cast<size_t>(i)]);
        ASSERT_TRUE(fetched.ok());
        EXPECT_EQ(fetched->GetValue(*schema_, 0).GetInteger(), i);
        EXPECT_EQ(fetched->GetValue(*schema_, 1).GetText(), "User_" + std::to_string(i) + "_description_padding_string");
        EXPECT_DOUBLE_EQ(fetched->GetValue(*schema_, 2).GetDouble(), static_cast<double>(i) * 1.5);
    }

    // Verify sequential scan via TableIterator
    int count = 0;
    for (auto it = heap->Begin(); it != heap->End(); ++it) {
        EXPECT_EQ(it->GetValue(*schema_, 0).GetInteger(), count);
        count++;
    }
    EXPECT_EQ(count, num_tuples);
}

TEST_F(TableHeapTest, UpdateAndDeleteOperations) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    BufferPoolManager bpm(4, &dm);
    auto heap_res = TableHeap::Create(&bpm);
    ASSERT_TRUE(heap_res.ok());
    auto heap = std::move(*heap_res);

    // Insert 3 tuples
    std::vector<RID> rids;
    for (int i = 0; i < 3; ++i) {
        std::vector<Value> vals;
        vals.emplace_back(i);
        vals.emplace_back("Item " + std::to_string(i));
        vals.emplace_back(10.0 * i);
        Tuple t(vals, *schema_);
        RID rid;
        ASSERT_TRUE(heap->InsertTuple(t, &rid).ok());
        rids.push_back(rid);
    }

    // Update tuple 1
    std::vector<Value> new_vals;
    new_vals.emplace_back(999);
    new_vals.emplace_back("Updated Item 1");
    new_vals.emplace_back(99.9);
    Tuple updated_tuple(new_vals, *schema_);
    ASSERT_TRUE(heap->UpdateTuple(updated_tuple, rids[1]).ok());

    auto read_back = heap->GetTuple(rids[1]);
    ASSERT_TRUE(read_back.ok());
    EXPECT_EQ(read_back->GetValue(*schema_, 0).GetInteger(), 999);
    EXPECT_EQ(read_back->GetValue(*schema_, 1).GetText(), "Updated Item 1");

    // Delete tuple 0
    ASSERT_TRUE(heap->MarkDelete(rids[0]).ok());
    EXPECT_FALSE(heap->GetTuple(rids[0]).ok());

    // Iterator should now yield only 2 tuples (tuple 1 and tuple 2)
    int iter_count = 0;
    for (auto it = heap->Begin(); it != heap->End(); ++it) {
        iter_count++;
    }
    EXPECT_EQ(iter_count, 2);
}

} // namespace
} // namespace nexus
