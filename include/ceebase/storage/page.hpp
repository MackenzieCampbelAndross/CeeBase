#pragma once

#include <array>
#include <cstring>
#include <shared_mutex>

#include "ceebase/common/config.hpp"
#include "ceebase/common/types.hpp"

namespace ceebase {

enum class PageType : uint32_t {
    UNKNOWN = 0,
    DB_HEADER = 1,
    SLOTTED_DATA = 2,
    BTREE_INTERNAL = 3,
    BTREE_LEAF = 4,
    FREE_PAGE = 5
};

[[nodiscard]] constexpr std::string_view PageTypeToString(PageType type) noexcept {
    switch (type) {
        case PageType::DB_HEADER:
            return "DB_HEADER";
        case PageType::SLOTTED_DATA:
            return "SLOTTED_DATA";
        case PageType::BTREE_INTERNAL:
            return "BTREE_INTERNAL";
        case PageType::BTREE_LEAF:
            return "BTREE_LEAF";
        case PageType::FREE_PAGE:
            return "FREE_PAGE";
        case PageType::UNKNOWN:
        default:
            return "UNKNOWN";
    }
}

// Fixed-size 4096-byte memory frame representation of a database page.
class Page {
    friend class BufferPoolManager;

public:
    Page() { ResetMemory(); }
    ~Page() = default;

    // Disallow copies, allow moves if necessary, but pages are normally pinned/referenced in frame pool
    Page(const Page&) = delete;
    Page& operator=(const Page&) = delete;

    [[nodiscard]] uint8_t* GetData() noexcept { return data_.data(); }
    [[nodiscard]] const uint8_t* GetData() const noexcept { return data_.data(); }

    [[nodiscard]] page_id_t GetPageId() const noexcept { return page_id_; }
    [[nodiscard]] int GetPinCount() const noexcept { return pin_count_; }
    [[nodiscard]] bool IsDirty() const noexcept { return is_dirty_; }
    [[nodiscard]] lsn_t GetLSN() const noexcept { return lsn_; }

    void SetDirty(bool is_dirty) noexcept { is_dirty_ = is_dirty; }
    void SetLSN(lsn_t lsn) noexcept { lsn_ = lsn; }

    // Read/Write synchronization latches
    void WLatch() { rwlatch_.lock(); }
    void WUnlatch() { rwlatch_.unlock(); }
    void RLatch() { rwlatch_.lock_shared(); }
    void RUnlatch() { rwlatch_.unlock_shared(); }

    void ResetMemory() {
        data_.fill(0);
        page_id_ = INVALID_PAGE_ID;
        pin_count_ = 0;
        is_dirty_ = false;
        lsn_ = INVALID_LSN;
    }

private:
    std::array<uint8_t, PAGE_SIZE> data_{};
    page_id_t page_id_{INVALID_PAGE_ID};
    int pin_count_{0};
    bool is_dirty_{false};
    lsn_t lsn_{INVALID_LSN};
    std::shared_mutex rwlatch_{};
};

} // namespace ceebase
