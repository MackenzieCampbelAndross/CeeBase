#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

#include "nexus/common/config.hpp"
#include "nexus/common/types.hpp"
#include "nexus/index/b_plus_tree_page.hpp"

namespace nexus {

struct LeafEntry {
    int64_t key{0};
    RID rid{};

    bool operator==(const LeafEntry& other) const noexcept {
        return key == other.key && rid == other.rid;
    }
};

class BPlusTreeLeafPage : public BPlusTreePage {
public:
    static constexpr size_t HEADER_SIZE = sizeof(BPlusTreePage) + (sizeof(page_id_t) * 2);
    static constexpr size_t LEAF_PAGE_SLOT_COUNT = (PAGE_SIZE - HEADER_SIZE) / sizeof(LeafEntry);

    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = static_cast<int>(LEAF_PAGE_SLOT_COUNT)) {
        SetPageType(PageType::BTREE_LEAF);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(parent_id);
        SetPageId(page_id);
        next_page_id_ = INVALID_PAGE_ID;
        prev_page_id_ = INVALID_PAGE_ID;
    }

    [[nodiscard]] page_id_t GetNextPageId() const noexcept { return next_page_id_; }
    void SetNextPageId(page_id_t next_page_id) noexcept { next_page_id_ = next_page_id; }

    [[nodiscard]] page_id_t GetPrevPageId() const noexcept { return prev_page_id_; }
    void SetPrevPageId(page_id_t prev_page_id) noexcept { prev_page_id_ = prev_page_id; }

    [[nodiscard]] int64_t KeyAt(int index) const noexcept { return array_[index].key; }
    [[nodiscard]] const RID& ValueAt(int index) const noexcept { return array_[index].rid; }

    [[nodiscard]] int KeyIndex(int64_t key) const noexcept {
        int low = 0;
        int high = GetSize() - 1;
        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (array_[mid].key >= key) {
                high = mid - 1;
            } else {
                low = mid + 1;
            }
        }
        return low;
    }

    [[nodiscard]] bool Lookup(int64_t key, RID* result) const noexcept {
        int idx = KeyIndex(key);
        if (idx < GetSize() && array_[idx].key == key) {
            if (result) {
                *result = array_[idx].rid;
            }
            return true;
        }
        return false;
    }

    int Insert(int64_t key, const RID& value) {
        int idx = KeyIndex(key);
        if (idx < GetSize() && array_[idx].key == key) {
            // Key already exists, update value
            array_[idx].rid = value;
            return GetSize();
        }

        // Shift elements right
        for (int i = GetSize(); i > idx; --i) {
            array_[i] = array_[i - 1];
        }
        array_[idx].key = key;
        array_[idx].rid = value;
        IncreaseSize(1);
        return GetSize();
    }

    int RemoveAndDeleteRecord(int64_t key) {
        int idx = KeyIndex(key);
        if (idx >= GetSize() || array_[idx].key != key) {
            return GetSize();
        }

        // Shift elements left
        for (int i = idx; i < GetSize() - 1; ++i) {
            array_[i] = array_[i + 1];
        }
        IncreaseSize(-1);
        return GetSize();
    }

    void MoveHalfTo(BPlusTreeLeafPage* recipient) {
        int total = GetSize();
        int move_count = total / 2;
        int start_idx = total - move_count;

        for (int i = start_idx; i < total; ++i) {
            recipient->array_[recipient->GetSize()] = array_[i];
            recipient->IncreaseSize(1);
        }
        SetSize(start_idx);
    }

    void MoveAllTo(BPlusTreeLeafPage* recipient) {
        for (int i = 0; i < GetSize(); ++i) {
            recipient->array_[recipient->GetSize()] = array_[i];
            recipient->IncreaseSize(1);
        }
        recipient->SetNextPageId(next_page_id_);
        SetSize(0);
    }

    void MoveFirstToEndOf(BPlusTreeLeafPage* recipient) {
        recipient->array_[recipient->GetSize()] = array_[0];
        recipient->IncreaseSize(1);

        for (int i = 0; i < GetSize() - 1; ++i) {
            array_[i] = array_[i + 1];
        }
        IncreaseSize(-1);
    }

    void MoveLastToFrontOf(BPlusTreeLeafPage* recipient) {
        LeafEntry last_entry = array_[GetSize() - 1];
        IncreaseSize(-1);

        for (int i = recipient->GetSize(); i > 0; --i) {
            recipient->array_[i] = recipient->array_[i - 1];
        }
        recipient->array_[0] = last_entry;
        recipient->IncreaseSize(1);
    }

private:
    page_id_t next_page_id_{INVALID_PAGE_ID};
    page_id_t prev_page_id_{INVALID_PAGE_ID};
    LeafEntry array_[LEAF_PAGE_SLOT_COUNT];
};

} // namespace nexus
