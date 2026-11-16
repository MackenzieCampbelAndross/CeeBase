#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include "nexus/common/byte_utils.hpp"
#include "nexus/common/config.hpp"
#include "nexus/common/result.hpp"
#include "nexus/common/status.hpp"
#include "nexus/common/types.hpp"
#include "nexus/storage/page.hpp"

namespace nexus {

// Slot entry in the directory: (offset from page start, length in bytes)
struct Slot {
    uint16_t offset{0};
    uint16_t length{0};

    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        return offset == 0 && length == 0;
    }
};

class SlottedPage {
public:
    static constexpr size_t HEADER_SIZE = 32;
    static constexpr size_t CHECKSUM_SIZE = 4;
    static constexpr size_t FOOTER_OFFSET = PAGE_SIZE - CHECKSUM_SIZE; // 4092
    static constexpr size_t MAX_USABLE_SPACE = FOOTER_OFFSET - HEADER_SIZE; // 4060 bytes

    explicit SlottedPage(uint8_t* page_data) : data_(page_data) {}

    // Initialize a new empty slotted data page
    void Init(page_id_t page_id, page_id_t prev_page = INVALID_PAGE_ID, page_id_t next_page = INVALID_PAGE_ID);

    // Page Header Accessors
    [[nodiscard]] PageType GetPageType() const noexcept;
    [[nodiscard]] page_id_t GetPageId() const noexcept;
    [[nodiscard]] lsn_t GetLSN() const noexcept;
    [[nodiscard]] page_id_t GetPrevPageId() const noexcept;
    [[nodiscard]] page_id_t GetNextPageId() const noexcept;
    [[nodiscard]] uint16_t GetSlotCount() const noexcept;
    [[nodiscard]] uint16_t GetFreeSpacePointer() const noexcept;

    void SetLSN(lsn_t lsn) noexcept;
    void SetPrevPageId(page_id_t prev_page) noexcept;
    void SetNextPageId(page_id_t next_page) noexcept;

    // Free Space Calculations
    [[nodiscard]] size_t GetFreeSpaceRemaining() const noexcept;

    // Slot directory inspection
    [[nodiscard]] Slot GetSlot(slot_id_t slot_id) const noexcept;

    // Record CRUD operations
    Result<slot_id_t> InsertRecord(std::span<const uint8_t> record);
    Result<std::vector<uint8_t>> GetRecord(slot_id_t slot_id) const;
    Status UpdateRecord(slot_id_t slot_id, std::span<const uint8_t> new_record);
    Status DeleteRecord(slot_id_t slot_id);

    // Defragment page records, eliminating gaps left by deleted or resized tuples
    void Defragment();

    // Checksum computation and verification
    void UpdateChecksum() noexcept;
    [[nodiscard]] bool VerifyChecksum() const noexcept;

private:
    void SetSlot(slot_id_t slot_id, const Slot& slot) noexcept;
    void SetSlotCount(uint16_t count) noexcept;
    void SetFreeSpacePointer(uint16_t ptr) noexcept;

    uint8_t* data_;
};

} // namespace nexus
