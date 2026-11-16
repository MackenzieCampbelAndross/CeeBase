#include "nexus/record/table_heap.hpp"

#include "nexus/record/slotted_page.hpp"

namespace nexus {

Result<std::unique_ptr<TableHeap>> TableHeap::Create(BufferPoolManager* bpm) {
    page_id_t first_page_id = INVALID_PAGE_ID;
    auto page_res = bpm->NewPage(&first_page_id);
    if (!page_res.ok()) {
        return page_res.status();
    }

    Page* page = *page_res;
    SlottedPage slotted(page->GetData());
    slotted.Init(first_page_id, INVALID_PAGE_ID, INVALID_PAGE_ID);

    (void)bpm->UnpinPage(first_page_id, true);

    return std::make_unique<TableHeap>(bpm, first_page_id);
}

std::unique_ptr<TableHeap> TableHeap::Open(BufferPoolManager* bpm, page_id_t first_page_id) {
    return std::make_unique<TableHeap>(bpm, first_page_id);
}

Status TableHeap::InsertTuple(const Tuple& tuple, RID* rid) {
    if (tuple.GetLength() == 0) {
        return Status::InvalidArgument("TableHeap: Cannot insert zero-length tuple");
    }

    page_id_t cur_page_id = first_page_id_;
    std::span<const uint8_t> tuple_span(tuple.GetData(), tuple.GetLength());

    while (cur_page_id != INVALID_PAGE_ID) {
        auto page_res = bpm_->FetchPage(cur_page_id);
        if (!page_res.ok()) {
            return page_res.status();
        }

        Page* page = *page_res;
        SlottedPage slotted(page->GetData());

        auto insert_res = slotted.InsertRecord(tuple_span);
        if (insert_res.ok()) {
            *rid = RID(cur_page_id, *insert_res);
            (void)bpm_->UnpinPage(cur_page_id, true);
            return Status::Ok();
        }

        // Current page is full, advance to next page in chain
        page_id_t next_page_id = slotted.GetNextPageId();
        if (next_page_id != INVALID_PAGE_ID) {
            (void)bpm_->UnpinPage(cur_page_id, false);
            cur_page_id = next_page_id;
            continue;
        }

        // Reached end of page list without finding space: allocate new page
        page_id_t new_page_id = INVALID_PAGE_ID;
        auto new_page_res = bpm_->NewPage(&new_page_id);
        if (!new_page_res.ok()) {
            (void)bpm_->UnpinPage(cur_page_id, false);
            return new_page_res.status();
        }

        Page* new_page = *new_page_res;
        SlottedPage new_slotted(new_page->GetData());
        new_slotted.Init(new_page_id, cur_page_id, INVALID_PAGE_ID);

        // Link previous page to new page
        slotted.SetNextPageId(new_page_id);
        (void)bpm_->UnpinPage(cur_page_id, true);

        // Insert tuple into the newly allocated page
        auto new_insert_res = new_slotted.InsertRecord(tuple_span);
        if (!new_insert_res.ok()) {
            (void)bpm_->UnpinPage(new_page_id, false);
            return new_insert_res.status();
        }

        *rid = RID(new_page_id, *new_insert_res);
        (void)bpm_->UnpinPage(new_page_id, true);
        return Status::Ok();
    }

    return Status::Internal("TableHeap: Failed to insert tuple");
}

Result<Tuple> TableHeap::GetTuple(const RID& rid) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("TableHeap: Invalid RID requested");
    }

    auto page_res = bpm_->FetchPage(rid.page_id);
    if (!page_res.ok()) {
        return page_res.status();
    }

    Page* page = *page_res;
    SlottedPage slotted(page->GetData());
    auto rec_res = slotted.GetRecord(rid.slot_id);
    (void)bpm_->UnpinPage(rid.page_id, false);

    if (!rec_res.ok()) {
        return rec_res.status();
    }

    return Tuple(std::move(*rec_res), rid);
}

Status TableHeap::UpdateTuple(const Tuple& tuple, const RID& rid) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("TableHeap: Invalid RID for update");
    }

    auto page_res = bpm_->FetchPage(rid.page_id);
    if (!page_res.ok()) {
        return page_res.status();
    }

    Page* page = *page_res;
    SlottedPage slotted(page->GetData());
    std::span<const uint8_t> tuple_span(tuple.GetData(), tuple.GetLength());

    Status s = slotted.UpdateRecord(rid.slot_id, tuple_span);
    (void)bpm_->UnpinPage(rid.page_id, s.ok());
    return s;
}

Status TableHeap::MarkDelete(const RID& rid) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("TableHeap: Invalid RID for deletion");
    }

    auto page_res = bpm_->FetchPage(rid.page_id);
    if (!page_res.ok()) {
        return page_res.status();
    }

    Page* page = *page_res;
    SlottedPage slotted(page->GetData());
    Status s = slotted.DeleteRecord(rid.slot_id);
    (void)bpm_->UnpinPage(rid.page_id, s.ok());
    return s;
}

Status TableHeap::WriteTupleAtRID(const Tuple& tuple, const RID& rid) {
    if (!rid.IsValid()) {
        return Status::InvalidArgument("TableHeap: Invalid RID for WriteTupleAtRID");
    }

    auto page_res = bpm_->FetchPage(rid.page_id);
    if (!page_res.ok()) {
        return page_res.status();
    }

    Page* page = *page_res;
    SlottedPage slotted(page->GetData());
    std::span<const uint8_t> tuple_span(tuple.GetData(), tuple.GetLength());

    Status s = slotted.SetRecordAtSlot(rid.slot_id, tuple_span);
    (void)bpm_->UnpinPage(rid.page_id, s.ok());
    return s;
}

// TableIterator implementation
TableIterator::TableIterator(TableHeap* heap, RID rid) : heap_(heap), current_rid_(rid) {
    if (current_rid_.IsValid()) {
        AdvanceToNextValid();
    }
}

bool TableIterator::operator==(const TableIterator& other) const noexcept {
    return heap_ == other.heap_ && current_rid_ == other.current_rid_;
}

TableIterator& TableIterator::operator++() {
    if (current_rid_.IsValid()) {
        current_rid_.slot_id++;
        AdvanceToNextValid();
    }
    return *this;
}

void TableIterator::AdvanceToNextValid() {
    while (current_rid_.page_id != INVALID_PAGE_ID) {
        auto page_res = heap_->bpm_->FetchPage(current_rid_.page_id);
        if (!page_res.ok()) {
            current_rid_ = RID();
            current_tuple_ = Tuple();
            return;
        }

        Page* page = *page_res;
        SlottedPage slotted(page->GetData());
        const uint16_t slot_count = slotted.GetSlotCount();

        while (current_rid_.slot_id < slot_count) {
            Slot slot = slotted.GetSlot(current_rid_.slot_id);
            if (!slot.IsEmpty()) {
                auto rec_res = slotted.GetRecord(current_rid_.slot_id);
                if (rec_res.ok()) {
                    current_tuple_ = Tuple(std::move(*rec_res), current_rid_);
                    (void)heap_->bpm_->UnpinPage(current_rid_.page_id, false);
                    return;
                }
            }
            current_rid_.slot_id++;
        }

        // Advanced past all slots in this page; proceed to next page
        page_id_t next_page_id = slotted.GetNextPageId();
        (void)heap_->bpm_->UnpinPage(current_rid_.page_id, false);
        current_rid_.page_id = next_page_id;
        current_rid_.slot_id = 0;
    }

    current_rid_ = RID();
    current_tuple_ = Tuple();
}

TableIterator TableHeap::Begin() {
    return TableIterator(this, RID(first_page_id_, 0));
}

TableIterator TableHeap::End() {
    return TableIterator(this, RID());
}

} // namespace nexus
