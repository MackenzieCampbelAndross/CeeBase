#pragma once

#include <cstdint>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/common/config.hpp"
#include "ceebase/common/types.hpp"
#include "ceebase/index/b_plus_tree_page.hpp"

namespace ceebase {

struct InternalEntry {
    int64_t key{0};
    page_id_t page_id{INVALID_PAGE_ID};

    bool operator==(const InternalEntry& other) const noexcept {
        return key == other.key && page_id == other.page_id;
    }
};

class BPlusTreeInternalPage : public BPlusTreePage {
public:
    static constexpr size_t HEADER_SIZE = sizeof(BPlusTreePage);
    static constexpr size_t INTERNAL_PAGE_SLOT_COUNT = (PAGE_SIZE - HEADER_SIZE) / sizeof(InternalEntry);

    void Init(page_id_t page_id, page_id_t parent_id = INVALID_PAGE_ID, int max_size = static_cast<int>(INTERNAL_PAGE_SLOT_COUNT)) {
        SetPageType(PageType::BTREE_INTERNAL);
        SetSize(0);
        SetMaxSize(max_size);
        SetParentPageId(parent_id);
        SetPageId(page_id);
    }

    [[nodiscard]] int64_t KeyAt(int index) const noexcept { return array_[index].key; }
    void SetKeyAt(int index, int64_t key) noexcept { array_[index].key = key; }

    [[nodiscard]] page_id_t ValueAt(int index) const noexcept { return array_[index].page_id; }
    void SetValueAt(int index, page_id_t value) noexcept { array_[index].page_id = value; }

    [[nodiscard]] int ValueIndex(page_id_t value) const noexcept {
        for (int i = 0; i < GetSize(); ++i) {
            if (array_[i].page_id == value) {
                return i;
            }
        }
        return -1;
    }

    [[nodiscard]] page_id_t Lookup(int64_t key) const noexcept {
        // array_[0].key is ignored; array_[0].page_id is for keys < array_[1].key
        int low = 1;
        int high = GetSize() - 1;
        int target = 0;

        while (low <= high) {
            int mid = low + (high - low) / 2;
            if (array_[mid].key <= key) {
                target = mid;
                low = mid + 1;
            } else {
                high = mid - 1;
            }
        }
        return array_[target].page_id;
    }

    void Populate(page_id_t val0, int64_t key1, page_id_t val1) {
        SetValueAt(0, val0);
        SetKeyAt(1, key1);
        SetValueAt(1, val1);
        SetSize(2);
    }

    int InsertNodeAfter(page_id_t old_value, int64_t new_key, page_id_t new_value) {
        int idx = ValueIndex(old_value);
        if (idx == -1) {
            return GetSize();
        }

        for (int i = GetSize(); i > idx + 1; --i) {
            array_[i] = array_[i - 1];
        }
        array_[idx + 1].key = new_key;
        array_[idx + 1].page_id = new_value;
        IncreaseSize(1);
        return GetSize();
    }

    void MoveHalfTo(BPlusTreeInternalPage* recipient, BufferPoolManager* bpm) {
        int total = GetSize();
        int move_count = total / 2;
        int start_idx = total - move_count;

        for (int i = start_idx; i < total; ++i) {
            recipient->array_[recipient->GetSize()] = array_[i];
            recipient->IncreaseSize(1);

            // Update parent pointer in child page
            auto child_res = bpm->FetchPage(array_[i].page_id);
            if (child_res.ok()) {
                auto* child = reinterpret_cast<BPlusTreePage*>((*child_res)->GetData());
                child->SetParentPageId(recipient->GetPageId());
                (void)bpm->UnpinPage(array_[i].page_id, true);
            }
        }
        SetSize(start_idx);
    }

    void MoveAllTo(BPlusTreeInternalPage* recipient, int64_t middle_key, BufferPoolManager* bpm) {
        // array_[0].key is set to the parent separator middle_key
        SetKeyAt(0, middle_key);

        for (int i = 0; i < GetSize(); ++i) {
            recipient->array_[recipient->GetSize()] = array_[i];
            recipient->IncreaseSize(1);

            auto child_res = bpm->FetchPage(array_[i].page_id);
            if (child_res.ok()) {
                auto* child = reinterpret_cast<BPlusTreePage*>((*child_res)->GetData());
                child->SetParentPageId(recipient->GetPageId());
                (void)bpm->UnpinPage(array_[i].page_id, true);
            }
        }
        SetSize(0);
    }

    void MoveFirstToEndOf(BPlusTreeInternalPage* recipient, int64_t middle_key, BufferPoolManager* bpm) {
        InternalEntry entry = array_[0];
        entry.key = middle_key;
        recipient->array_[recipient->GetSize()] = entry;
        recipient->IncreaseSize(1);

        auto child_res = bpm->FetchPage(entry.page_id);
        if (child_res.ok()) {
            auto* child = reinterpret_cast<BPlusTreePage*>((*child_res)->GetData());
            child->SetParentPageId(recipient->GetPageId());
            (void)bpm->UnpinPage(entry.page_id, true);
        }

        for (int i = 0; i < GetSize() - 1; ++i) {
            array_[i] = array_[i + 1];
        }
        IncreaseSize(-1);
    }

    void MoveLastToFrontOf(BPlusTreeInternalPage* recipient, int64_t middle_key, BufferPoolManager* bpm) {
        InternalEntry last_entry = array_[GetSize() - 1];
        IncreaseSize(-1);

        for (int i = recipient->GetSize(); i > 0; --i) {
            recipient->array_[i] = recipient->array_[i - 1];
        }
        recipient->array_[1].key = middle_key;
        recipient->array_[0].page_id = last_entry.page_id;
        recipient->IncreaseSize(1);

        auto child_res = bpm->FetchPage(last_entry.page_id);
        if (child_res.ok()) {
            auto* child = reinterpret_cast<BPlusTreePage*>((*child_res)->GetData());
            child->SetParentPageId(recipient->GetPageId());
            (void)bpm->UnpinPage(last_entry.page_id, true);
        }
    }

    void Remove(int index) {
        for (int i = index; i < GetSize() - 1; ++i) {
            array_[i] = array_[i + 1];
        }
        IncreaseSize(-1);
    }

private:
    InternalEntry array_[INTERNAL_PAGE_SLOT_COUNT];
};

} // namespace ceebase
