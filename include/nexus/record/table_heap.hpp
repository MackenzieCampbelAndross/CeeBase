#pragma once

#include <memory>
#include <vector>

#include "nexus/buffer/buffer_pool_manager.hpp"
#include "nexus/common/result.hpp"
#include "nexus/common/status.hpp"
#include "nexus/common/types.hpp"
#include "nexus/record/tuple.hpp"

namespace nexus {

class TableHeap;

class TableIterator {
public:
    TableIterator(TableHeap* heap, RID rid);
    ~TableIterator() = default;

    [[nodiscard]] bool operator==(const TableIterator& other) const noexcept;
    [[nodiscard]] bool operator!=(const TableIterator& other) const noexcept { return !(*this == other); }

    TableIterator& operator++();
    [[nodiscard]] const Tuple& operator*() const { return current_tuple_; }
    [[nodiscard]] const Tuple* operator->() const { return &current_tuple_; }

private:
    void AdvanceToNextValid();

    TableHeap* heap_{nullptr};
    RID current_rid_{INVALID_PAGE_ID, INVALID_SLOT_ID};
    Tuple current_tuple_{};
};

class TableHeap {
    friend class TableIterator;

public:
    // Create a new table heap, allocating its initial root slotted page
    static Result<std::unique_ptr<TableHeap>> Create(BufferPoolManager* bpm);

    // Open an existing table heap starting from an existing first page ID
    static std::unique_ptr<TableHeap> Open(BufferPoolManager* bpm, page_id_t first_page_id);

    TableHeap(BufferPoolManager* bpm, page_id_t first_page_id)
        : bpm_(bpm), first_page_id_(first_page_id) {}

    [[nodiscard]] page_id_t GetFirstPageId() const noexcept { return first_page_id_; }

    // Heap CRUD operations
    Status InsertTuple(const Tuple& tuple, RID* rid);
    Result<Tuple> GetTuple(const RID& rid);
    Status UpdateTuple(const Tuple& tuple, const RID& rid);
    Status MarkDelete(const RID& rid);
    Status WriteTupleAtRID(const Tuple& tuple, const RID& rid);

    // Iteration support
    [[nodiscard]] TableIterator Begin();
    [[nodiscard]] TableIterator End();

private:
    BufferPoolManager* bpm_;
    page_id_t first_page_id_{INVALID_PAGE_ID};
};

} // namespace nexus
