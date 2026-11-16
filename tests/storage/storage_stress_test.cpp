#include <gtest/gtest.h>

#include <filesystem>
#include <map>
#include <random>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/record/schema.hpp"
#include "ceebase/record/table_heap.hpp"
#include "ceebase/storage/disk_manager.hpp"

namespace ceebase {
namespace {

class StorageStressTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "ceebase_stress_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "stress.nxdb";
        std::filesystem::remove(db_path_);

        std::vector<Column> cols;
        cols.emplace_back("id", DataType::INTEGER, false);
        cols.emplace_back("payload", DataType::TEXT, false);
        schema_ = std::make_unique<Schema>(std::move(cols));
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
    std::unique_ptr<Schema> schema_;
};

TEST_F(StorageStressTest, RandomizedCRUDAndProcessRestart) {
    page_id_t root_page = INVALID_PAGE_ID;
    std::map<int, std::pair<RID, std::string>> active_records;

    // Phase A: Run randomized insertions, updates, and deletions
    {
        DiskManager dm;
        ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

        // Small buffer pool (8 frames) forces heavy page eviction under 300 records
        BufferPoolManager bpm(8, &dm);
        auto heap_res = TableHeap::Create(&bpm);
        ASSERT_TRUE(heap_res.ok());
        auto heap = std::move(*heap_res);
        root_page = heap->GetFirstPageId();

        std::mt19937 rng(42);
        std::uniform_int_distribution<int> len_dist(20, 200);

        for (int i = 0; i < 300; ++i) {
            std::string text_payload(static_cast<size_t>(len_dist(rng)), static_cast<char>('A' + (i % 26)));
            std::vector<Value> vals;
            vals.emplace_back(i);
            vals.emplace_back(text_payload);

            Tuple t(vals, *schema_);
            RID rid;
            ASSERT_TRUE(heap->InsertTuple(t, &rid).ok());
            active_records[i] = {rid, text_payload};
        }

        // Randomly update 50 records
        for (int i = 0; i < 50; ++i) {
            int key = i * 4;
            if (active_records.find(key) != active_records.end()) {
                std::string updated_payload = "UPDATED_" + std::to_string(key);
                std::vector<Value> vals;
                vals.emplace_back(key);
                vals.emplace_back(updated_payload);

                Tuple t(vals, *schema_);
                ASSERT_TRUE(heap->UpdateTuple(t, active_records[key].first).ok());
                active_records[key].second = updated_payload;
            }
        }

        // Randomly delete 50 records
        for (int i = 0; i < 50; ++i) {
            int key = i * 5;
            if (active_records.find(key) != active_records.end()) {
                ASSERT_TRUE(heap->MarkDelete(active_records[key].first).ok());
                active_records.erase(key);
            }
        }

        // BufferPoolManager destructor will flush all dirty frames on scope exit
    }

    // Phase B: Simulate Crash / Process Restart
    // Reopen database from persistent disk file with brand new manager instances
    {
        DiskManager dm;
        ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
        BufferPoolManager bpm(8, &dm);

        auto heap = TableHeap::Open(&bpm, root_page);

        // Verify every active record survived restart with exact payload
        for (const auto& [key, entry] : active_records) {
            const RID& rid = entry.first;
            const std::string& expected_text = entry.second;

            auto fetched = heap->GetTuple(rid);
            ASSERT_TRUE(fetched.ok()) << "Failed to fetch record key " << key << " at RID " << rid.ToString();
            EXPECT_EQ(fetched->GetValue(*schema_, 0).GetInteger(), key);
            EXPECT_EQ(fetched->GetValue(*schema_, 1).GetText(), expected_text);
        }

        // Verify sequential scan yields exact active count
        size_t scan_count = 0;
        for (auto it = heap->Begin(); it != heap->End(); ++it) {
            scan_count++;
        }
        EXPECT_EQ(scan_count, active_records.size());
    }
}

} // namespace
} // namespace ceebase
