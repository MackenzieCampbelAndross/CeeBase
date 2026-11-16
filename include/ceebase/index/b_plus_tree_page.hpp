#pragma once

#include <cstdint>
#include <string_view>

#include "ceebase/common/config.hpp"
#include "ceebase/common/types.hpp"
#include "ceebase/storage/page.hpp"

namespace ceebase {

// Common header for both internal and leaf B+ Tree pages
class BPlusTreePage {
public:
    [[nodiscard]] PageType GetPageType() const noexcept { return page_type_; }
    void SetPageType(PageType type) noexcept { page_type_ = type; }

    [[nodiscard]] bool IsLeafPage() const noexcept { return page_type_ == PageType::BTREE_LEAF; }
    [[nodiscard]] bool IsRootPage() const noexcept { return parent_page_id_ == INVALID_PAGE_ID; }

    [[nodiscard]] int GetSize() const noexcept { return size_; }
    void SetSize(int size) noexcept { size_ = size; }
    void IncreaseSize(int amount) noexcept { size_ += amount; }

    [[nodiscard]] int GetMaxSize() const noexcept { return max_size_; }
    void SetMaxSize(int max_size) noexcept { max_size_ = max_size; }

    [[nodiscard]] int GetMinSize() const noexcept {
        if (IsRootPage()) {
            return IsLeafPage() ? 1 : 2;
        }
        return (max_size_ + 1) / 2;
    }

    [[nodiscard]] page_id_t GetParentPageId() const noexcept { return parent_page_id_; }
    void SetParentPageId(page_id_t parent_page_id) noexcept { parent_page_id_ = parent_page_id; }

    [[nodiscard]] page_id_t GetPageId() const noexcept { return page_id_; }
    void SetPageId(page_id_t page_id) noexcept { page_id_ = page_id; }

    [[nodiscard]] lsn_t GetLSN() const noexcept { return lsn_; }
    void SetLSN(lsn_t lsn) noexcept { lsn_ = lsn; }

protected:
    PageType page_type_{PageType::UNKNOWN};
    lsn_t lsn_{INVALID_LSN};
    int size_{0};
    int max_size_{0};
    page_id_t parent_page_id_{INVALID_PAGE_ID};
    page_id_t page_id_{INVALID_PAGE_ID};
};

} // namespace ceebase
