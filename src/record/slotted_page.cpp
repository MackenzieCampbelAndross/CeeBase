#include "nexus/record/slotted_page.hpp"

#include <algorithm>
#include <cstring>

namespace nexus {

void SlottedPage::Init(page_id_t page_id, page_id_t prev_page, page_id_t next_page) {
    std::memset(data_, 0, PAGE_SIZE);

    EncodeU32(static_cast<uint32_t>(PageType::SLOTTED_DATA), data_);
    EncodeU32(page_id, data_ + 4);
    EncodeU64(0, data_ + 8); // LSN
    EncodeU32(prev_page, data_ + 16);
    EncodeU32(next_page, data_ + 20);
    EncodeU16(0, data_ + 24); // slot_count = 0
    EncodeU16(static_cast<uint16_t>(FOOTER_OFFSET), data_ + 26); // free_space_pointer = 4092
    EncodeU32(0, data_ + 28); // reserved

    UpdateChecksum();
}

PageType SlottedPage::GetPageType() const noexcept {
    return static_cast<PageType>(DecodeU32(data_));
}

page_id_t SlottedPage::GetPageId() const noexcept {
    return DecodeU32(data_ + 4);
}

lsn_t SlottedPage::GetLSN() const noexcept {
    return DecodeU64(data_ + 8);
}

page_id_t SlottedPage::GetPrevPageId() const noexcept {
    return DecodeU32(data_ + 16);
}

page_id_t SlottedPage::GetNextPageId() const noexcept {
    return DecodeU32(data_ + 20);
}

uint16_t SlottedPage::GetSlotCount() const noexcept {
    return DecodeU16(data_ + 24);
}

uint16_t SlottedPage::GetFreeSpacePointer() const noexcept {
    return DecodeU16(data_ + 26);
}

void SlottedPage::SetLSN(lsn_t lsn) noexcept {
    EncodeU64(lsn, data_ + 8);
}

void SlottedPage::SetPrevPageId(page_id_t prev_page) noexcept {
    EncodeU32(prev_page, data_ + 16);
}

void SlottedPage::SetNextPageId(page_id_t next_page) noexcept {
    EncodeU32(next_page, data_ + 20);
}

void SlottedPage::SetSlotCount(uint16_t count) noexcept {
    EncodeU16(count, data_ + 24);
}

void SlottedPage::SetFreeSpacePointer(uint16_t ptr) noexcept {
    EncodeU16(ptr, data_ + 26);
}

size_t SlottedPage::GetFreeSpaceRemaining() const noexcept {
    const uint16_t slot_count = GetSlotCount();
    const uint16_t free_ptr = GetFreeSpacePointer();
    const size_t slot_dir_end = HEADER_SIZE + (slot_count * sizeof(Slot));
    if (free_ptr < slot_dir_end) {
        return 0;
    }
    return free_ptr - slot_dir_end;
}

Slot SlottedPage::GetSlot(slot_id_t slot_id) const noexcept {
    if (slot_id >= GetSlotCount()) {
        return Slot{0, 0};
    }
    const size_t slot_offset = HEADER_SIZE + (slot_id * sizeof(Slot));
    const uint16_t offset = DecodeU16(data_ + slot_offset);
    const uint16_t length = DecodeU16(data_ + slot_offset + 2);
    return Slot{offset, length};
}

void SlottedPage::SetSlot(slot_id_t slot_id, const Slot& slot) noexcept {
    const size_t slot_offset = HEADER_SIZE + (slot_id * sizeof(Slot));
    EncodeU16(slot.offset, data_ + slot_offset);
    EncodeU16(slot.length, data_ + slot_offset + 2);
}

Result<slot_id_t> SlottedPage::InsertRecord(std::span<const uint8_t> record) {
    if (record.empty()) {
        return Status::InvalidArgument("SlottedPage: Cannot insert empty record");
    }
    if (record.size() > MAX_USABLE_SPACE) {
        return Status::InvalidArgument("SlottedPage: Record size exceeds page capacity");
    }

    const uint16_t record_len = static_cast<uint16_t>(record.size());
    const uint16_t slot_count = GetSlotCount();

    // Check if an empty/deleted slot can be reused
    slot_id_t target_slot = INVALID_SLOT_ID;
    for (slot_id_t i = 0; i < slot_count; ++i) {
        Slot s = GetSlot(i);
        if (s.IsEmpty()) {
            target_slot = i;
            break;
        }
    }

    const size_t needed_space = (target_slot == INVALID_SLOT_ID)
                                    ? (record_len + sizeof(Slot))
                                    : record_len;

    if (GetFreeSpaceRemaining() < needed_space) {
        // Try defragmentation in case fragmented gaps exist
        Defragment();
        if (GetFreeSpaceRemaining() < needed_space) {
            return Status::OutOfRange("SlottedPage: Insufficient free space for record insertion");
        }
    }

    uint16_t free_ptr = GetFreeSpacePointer();
    free_ptr = static_cast<uint16_t>(free_ptr - record_len);
    SetFreeSpacePointer(free_ptr);

    // Copy record bytes into lower portion
    std::memcpy(data_ + free_ptr, record.data(), record_len);

    if (target_slot == INVALID_SLOT_ID) {
        target_slot = slot_count;
        SetSlotCount(slot_count + 1);
    }

    SetSlot(target_slot, Slot{free_ptr, record_len});
    UpdateChecksum();

    return target_slot;
}

Result<std::vector<uint8_t>> SlottedPage::GetRecord(slot_id_t slot_id) const {
    if (slot_id >= GetSlotCount()) {
        return Status::NotFound("SlottedPage: Slot ID out of range");
    }

    Slot slot = GetSlot(slot_id);
    if (slot.IsEmpty()) {
        return Status::NotFound("SlottedPage: Slot has been deleted");
    }

    if (slot.offset < HEADER_SIZE || slot.offset + slot.length > FOOTER_OFFSET) {
        return Status::Corruption("SlottedPage: Corrupted slot offset or length");
    }

    std::vector<uint8_t> record(slot.length);
    std::memcpy(record.data(), data_ + slot.offset, slot.length);
    return record;
}

Status SlottedPage::UpdateRecord(slot_id_t slot_id, std::span<const uint8_t> new_record) {
    if (slot_id >= GetSlotCount()) {
        return Status::NotFound("SlottedPage: Slot ID out of range for update");
    }

    Slot old_slot = GetSlot(slot_id);
    if (old_slot.IsEmpty()) {
        return Status::NotFound("SlottedPage: Attempting to update deleted slot");
    }

    const uint16_t new_len = static_cast<uint16_t>(new_record.size());

    // If new length fits exactly in-place
    if (new_len <= old_slot.length) {
        std::memcpy(data_ + old_slot.offset, new_record.data(), new_len);
        SetSlot(slot_id, Slot{old_slot.offset, new_len});
        UpdateChecksum();
        return Status::Ok();
    }

    // New length is larger: check if space is available
    const size_t additional_needed = new_len - old_slot.length;
    if (GetFreeSpaceRemaining() < additional_needed) {
        Defragment();
        // Refresh slot location after defragmentation
        old_slot = GetSlot(slot_id);
        if (GetFreeSpaceRemaining() < additional_needed) {
            return Status::OutOfRange("SlottedPage: Insufficient free space for expanded record");
        }
    }

    // Mark old slot space as empty, allocate new chunk
    uint16_t free_ptr = GetFreeSpacePointer();
    free_ptr = static_cast<uint16_t>(free_ptr - new_len);
    SetFreeSpacePointer(free_ptr);

    std::memcpy(data_ + free_ptr, new_record.data(), new_len);
    SetSlot(slot_id, Slot{free_ptr, new_len});
    UpdateChecksum();

    return Status::Ok();
}

Status SlottedPage::DeleteRecord(slot_id_t slot_id) {
    if (slot_id >= GetSlotCount()) {
        return Status::NotFound("SlottedPage: Slot ID out of range for deletion");
    }

    Slot slot = GetSlot(slot_id);
    if (slot.IsEmpty()) {
        return Status::NotFound("SlottedPage: Slot already deleted");
    }

    // Mark slot as empty
    SetSlot(slot_id, Slot{0, 0});

    // If deleting the last slot in the directory, decrement slot count
    uint16_t count = GetSlotCount();
    while (count > 0 && GetSlot(static_cast<slot_id_t>(count - 1)).IsEmpty()) {
        count--;
    }
    SetSlotCount(count);

    UpdateChecksum();
    return Status::Ok();
}

Status SlottedPage::SetRecordAtSlot(slot_id_t target_slot, std::span<const uint8_t> record) {
    if (record.empty()) {
        return Status::InvalidArgument("SlottedPage: Empty record");
    }
    if (record.size() > MAX_USABLE_SPACE) {
        return Status::InvalidArgument("SlottedPage: Record size exceeds page capacity");
    }

    const uint16_t record_len = static_cast<uint16_t>(record.size());
    uint16_t slot_count = GetSlotCount();

    if (target_slot < slot_count) {
        Slot old_slot = GetSlot(target_slot);
        if (!old_slot.IsEmpty()) {
            return UpdateRecord(target_slot, record);
        }
    }

    size_t needed_space = record_len;
    if (target_slot >= slot_count) {
        needed_space += (static_cast<size_t>(target_slot) + 1 - slot_count) * sizeof(Slot);
    }

    if (GetFreeSpaceRemaining() < needed_space) {
        Defragment();
        if (GetFreeSpaceRemaining() < needed_space) {
            return Status::OutOfRange("SlottedPage: Insufficient free space for SetRecordAtSlot");
        }
    }

    uint16_t free_ptr = GetFreeSpacePointer();
    free_ptr = static_cast<uint16_t>(free_ptr - record_len);
    SetFreeSpacePointer(free_ptr);

    std::memcpy(data_ + free_ptr, record.data(), record_len);

    if (target_slot >= slot_count) {
        for (slot_id_t i = slot_count; i < target_slot; ++i) {
            SetSlot(i, Slot{0, 0});
        }
        SetSlotCount(target_slot + 1);
    }

    SetSlot(target_slot, Slot{free_ptr, record_len});
    UpdateChecksum();
    return Status::Ok();
}

void SlottedPage::Defragment() {
    std::array<uint8_t, PAGE_SIZE> temp{};
    std::memcpy(temp.data(), data_, HEADER_SIZE);

    const uint16_t slot_count = GetSlotCount();
    uint16_t new_free_ptr = static_cast<uint16_t>(FOOTER_OFFSET);

    for (slot_id_t i = 0; i < slot_count; ++i) {
        Slot old_slot = GetSlot(i);
        if (old_slot.IsEmpty()) {
            // Write empty slot into temp
            const size_t slot_offset = HEADER_SIZE + (i * sizeof(Slot));
            EncodeU16(0, temp.data() + slot_offset);
            EncodeU16(0, temp.data() + slot_offset + 2);
        } else {
            new_free_ptr = static_cast<uint16_t>(new_free_ptr - old_slot.length);
            std::memcpy(temp.data() + new_free_ptr, data_ + old_slot.offset, old_slot.length);

            const size_t slot_offset = HEADER_SIZE + (i * sizeof(Slot));
            EncodeU16(new_free_ptr, temp.data() + slot_offset);
            EncodeU16(old_slot.length, temp.data() + slot_offset + 2);
        }
    }

    EncodeU16(new_free_ptr, temp.data() + 26); // free_space_pointer
    std::memcpy(data_, temp.data(), FOOTER_OFFSET);
    UpdateChecksum();
}

void SlottedPage::UpdateChecksum() noexcept {
    const uint32_t checksum = ComputeCRC32(std::span<const uint8_t>(data_, FOOTER_OFFSET));
    EncodeU32(checksum, data_ + FOOTER_OFFSET);
}

bool SlottedPage::VerifyChecksum() const noexcept {
    const uint32_t expected = DecodeU32(data_ + FOOTER_OFFSET);
    const uint32_t actual = ComputeCRC32(std::span<const uint8_t>(data_, FOOTER_OFFSET));
    return expected == actual;
}

} // namespace nexus
