#pragma once

#include <memory>
#include <mutex>
#include <optional>
#include <vector>

#include "nexus/buffer/buffer_pool_manager.hpp"
#include "nexus/common/result.hpp"
#include "nexus/common/status.hpp"
#include "nexus/common/types.hpp"
#include "nexus/index/b_plus_tree_internal_page.hpp"
#include "nexus/index/b_plus_tree_leaf_page.hpp"

namespace nexus {

class BPlusTreeIndex;

class IndexIterator {
public:
    IndexIterator() = default;
    IndexIterator(BufferPoolManager* bpm, page_id_t leaf_page_id, int index);
    ~IndexIterator();

    IndexIterator(const IndexIterator&) = delete;
    IndexIterator& operator=(const IndexIterator&) = delete;

    IndexIterator(IndexIterator&& other) noexcept;
    IndexIterator& operator=(IndexIterator&& other) noexcept;

    bool operator==(const IndexIterator& other) const noexcept;
    bool operator!=(const IndexIterator& other) const noexcept { return !(*this == other); }

    IndexIterator& operator++();
    [[nodiscard]] std::pair<int64_t, RID> operator*() const;

    [[nodiscard]] bool IsEnd() const noexcept { return leaf_page_id_ == INVALID_PAGE_ID; }

private:
    BufferPoolManager* bpm_{nullptr};
    page_id_t leaf_page_id_{INVALID_PAGE_ID};
    int index_{0};
    Page* page_{nullptr};
};

class BPlusTreeIndex {
public:
    explicit BPlusTreeIndex(std::string name, BufferPoolManager* bpm, page_id_t root_page_id = INVALID_PAGE_ID,
                            int leaf_max_size = BPlusTreeLeafPage::LEAF_PAGE_SLOT_COUNT,
                            int internal_max_size = BPlusTreeInternalPage::INTERNAL_PAGE_SLOT_COUNT);
    ~BPlusTreeIndex() = default;

    [[nodiscard]] const std::string& GetName() const noexcept { return name_; }
    [[nodiscard]] page_id_t GetRootPageId() const noexcept { return root_page_id_; }
    [[nodiscard]] bool IsEmpty() const noexcept { return root_page_id_ == INVALID_PAGE_ID; }

    // Core Operations
    Status Insert(int64_t key, const RID& value);
    bool GetValue(int64_t key, RID* result);
    Status Remove(int64_t key);

    // Range queries and scanning
    [[nodiscard]] IndexIterator Begin();
    [[nodiscard]] IndexIterator Begin(int64_t key);
    [[nodiscard]] IndexIterator End();

    // Diagnostics
    [[nodiscard]] size_t GetHeight();

private:
    Page* FindLeafPage(int64_t key, bool left_most = false);
    void StartNewTree(int64_t key, const RID& value);
    void InsertIntoLeaf(BPlusTreeLeafPage* leaf, int64_t key, const RID& value);
    void InsertIntoParent(BPlusTreePage* old_node, int64_t key, BPlusTreePage* new_node);
    template <typename N>
    N* Split(N* node);

    // Deletion helpers
    bool CoalesceOrRedistribute(BPlusTreePage* node);
    bool Coalesce(BPlusTreePage* neighbor_node, BPlusTreePage* node, BPlusTreeInternalPage* parent, int index);
    void Redistribute(BPlusTreePage* neighbor_node, BPlusTreePage* node, BPlusTreeInternalPage* parent, int index);
    bool AdjustRoot(BPlusTreePage* old_root_node);

    std::string name_;
    BufferPoolManager* bpm_;
    page_id_t root_page_id_{INVALID_PAGE_ID};
    int leaf_max_size_;
    int internal_max_size_;
    mutable std::mutex latch_{};
};

} // namespace nexus
