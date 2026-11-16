#include <gtest/gtest.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <random>

#include "nexus/storage/disk_manager.hpp"

namespace nexus {
namespace {

class DiskManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_dir_ = std::filesystem::temp_directory_path() / "nexus_disk_test";
        std::filesystem::create_directories(test_dir_);
        db_path_ = test_dir_ / "test_store.nxdb";
        std::filesystem::remove(db_path_);
    }

    void TearDown() override {
        std::filesystem::remove_all(test_dir_);
    }

    std::filesystem::path test_dir_;
    std::filesystem::path db_path_;
};

TEST_F(DiskManagerTest, CreateAndReopenDatabase) {
    {
        DiskManager dm;
        ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());
        EXPECT_TRUE(dm.IsOpen());
        EXPECT_EQ(dm.GetNumPages(), 1); // Header page is page 0

        // Allocate a page
        auto alloc_res = dm.AllocatePage();
        ASSERT_TRUE(alloc_res.ok());
        EXPECT_EQ(*alloc_res, 1);
        EXPECT_EQ(dm.GetNumPages(), 2);

        // Write test pattern to Page 1
        std::array<uint8_t, PAGE_SIZE> write_buf{};
        write_buf.fill(0xA5);
        ASSERT_TRUE(dm.WritePage(1, write_buf.data()).ok());
        dm.Close();
    }

    // Simulate process restart: Reopen database
    {
        DiskManager dm;
        ASSERT_TRUE(dm.OpenDatabase(db_path_).ok());
        EXPECT_TRUE(dm.IsOpen());
        EXPECT_EQ(dm.GetNumPages(), 2);

        // Read Page 1 and verify data integrity
        std::array<uint8_t, PAGE_SIZE> read_buf{};
        ASSERT_TRUE(dm.ReadPage(1, read_buf.data()).ok());
        for (size_t i = 0; i < PAGE_SIZE; ++i) {
            EXPECT_EQ(read_buf[i], 0xA5);
        }
    }
}

TEST_F(DiskManagerTest, CorruptedHeaderDetection) {
    {
        DiskManager dm;
        ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());
        dm.Close();
    }

    // Tamper with byte inside the file to invalidate CRC-32 checksum
    {
        std::fstream file(db_path_, std::ios::binary | std::ios::in | std::ios::out);
        file.seekp(10, std::ios::beg);
        char corrupt_byte = 0x7F;
        file.write(&corrupt_byte, 1);
        file.flush();
    }

    // Attempt to open corrupted database must fail with Status::Corruption
    {
        DiskManager dm;
        Status s = dm.OpenDatabase(db_path_);
        EXPECT_FALSE(s.ok());
        EXPECT_EQ(s.code(), StatusCode::Corruption);
    }
}

TEST_F(DiskManagerTest, PageDeallocationAndReuse) {
    DiskManager dm;
    ASSERT_TRUE(dm.CreateDatabase(db_path_).ok());

    auto p1 = dm.AllocatePage();
    auto p2 = dm.AllocatePage();
    ASSERT_TRUE(p1.ok());
    ASSERT_TRUE(p2.ok());
    EXPECT_EQ(*p1, 1);
    EXPECT_EQ(*p2, 2);

    // Deallocate page 1
    ASSERT_TRUE(dm.DeallocatePage(*p1).ok());

    // Next allocation should reuse page 1 from the free list
    auto p3 = dm.AllocatePage();
    ASSERT_TRUE(p3.ok());
    EXPECT_EQ(*p3, 1);

    // Subsequent allocation should allocate page 3
    auto p4 = dm.AllocatePage();
    ASSERT_TRUE(p4.ok());
    EXPECT_EQ(*p4, 3);
}

} // namespace
} // namespace nexus
