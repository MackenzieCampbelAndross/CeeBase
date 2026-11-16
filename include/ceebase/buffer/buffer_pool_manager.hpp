#pragma once

#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "ceebase/buffer/lru_replacer.hpp"
#include "ceebase/common/config.hpp"
#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/storage/disk_manager.hpp"
#include "ceebase/storage/page.hpp"

namespace ceebase {

class LogManager;

class BufferPoolManager {
public:
    BufferPoolManager(size_t pool_size, DiskManager* disk_manager, LogManager* log_manager = nullptr);
    ~BufferPoolManager();

    BufferPoolManager(const BufferPoolManager&) = delete;
    BufferPoolManager& operator=(const BufferPoolManager&) = delete;

    [[nodiscard]] size_t GetPoolSize() const noexcept { return pool_size_; }
    void SetLogManager(LogManager* log_manager) noexcept { log_manager_ = log_manager; }
    [[nodiscard]] LogManager* GetLogManager() const noexcept { return log_manager_; }

    // Fetch an existing page from the buffer pool or read it from disk into a frame.
    // Automatically increments the page's pin count.
    Result<Page*> FetchPage(page_id_t page_id);

    // Unpin a page in the buffer pool. If pin_count reaches 0, the page becomes evictable.
    Status UnpinPage(page_id_t page_id, bool is_dirty);

    // Write a specific page to disk if it is marked dirty.
    Status FlushPage(page_id_t page_id);

    // Flush all dirty pages currently in the buffer pool to disk.
    Status FlushAllPages();

    // Allocate a new physical page on disk and load it into a pinned buffer frame.
    Result<Page*> NewPage(page_id_t* page_id);

    // Deallocate a page from the buffer pool and release its storage in the disk manager.
    Status DeletePage(page_id_t page_id);

    // Diagnostic queries
    [[nodiscard]] size_t GetNumFreeFrames() const;
    [[nodiscard]] bool IsPageInPool(page_id_t page_id) const;

private:
    bool FindAvailableFrame(frame_id_t* frame_id);

    size_t pool_size_;
    DiskManager* disk_manager_;
    LogManager* log_manager_{nullptr};
    std::unique_ptr<LRUReplacer> replacer_;
    std::vector<Page> pages_;
    std::unordered_map<page_id_t, frame_id_t> page_table_{};
    std::list<frame_id_t> free_list_{};
    mutable std::mutex latch_{};
};

// RAII PageGuard: Automatically unpins the page upon going out of scope.
class PageGuard {
public:
    PageGuard() = default;
    PageGuard(BufferPoolManager* bpm, Page* page) : bpm_(bpm), page_(page), is_dirty_(false) {}
    ~PageGuard() { Drop(); }

    PageGuard(const PageGuard&) = delete;
    PageGuard& operator=(const PageGuard&) = delete;

    PageGuard(PageGuard&& other) noexcept
        : bpm_(other.bpm_), page_(other.page_), is_dirty_(other.is_dirty_) {
        other.bpm_ = nullptr;
        other.page_ = nullptr;
        other.is_dirty_ = false;
    }

    PageGuard& operator=(PageGuard&& other) noexcept {
        if (this != &other) {
            Drop();
            bpm_ = other.bpm_;
            page_ = other.page_;
            is_dirty_ = other.is_dirty_;
            other.bpm_ = nullptr;
            other.page_ = nullptr;
            other.is_dirty_ = false;
        }
        return *this;
    }

    void Drop() {
        if (bpm_ != nullptr && page_ != nullptr) {
            (void)bpm_->UnpinPage(page_->GetPageId(), is_dirty_);
            bpm_ = nullptr;
            page_ = nullptr;
            is_dirty_ = false;
        }
    }

    [[nodiscard]] bool IsValid() const noexcept { return page_ != nullptr; }
    [[nodiscard]] Page* Get() noexcept { return page_; }
    [[nodiscard]] const Page* Get() const noexcept { return page_; }
    [[nodiscard]] uint8_t* GetData() noexcept { return page_ ? page_->GetData() : nullptr; }
    [[nodiscard]] const uint8_t* GetData() const noexcept { return page_ ? page_->GetData() : nullptr; }
    [[nodiscard]] page_id_t GetPageId() const noexcept { return page_ ? page_->GetPageId() : INVALID_PAGE_ID; }

    void SetDirty(bool dirty) noexcept { is_dirty_ = dirty; }

    Page* operator->() noexcept { return page_; }
    const Page* operator->() const noexcept { return page_; }

private:
    BufferPoolManager* bpm_{nullptr};
    Page* page_{nullptr};
    bool is_dirty_{false};
};

} // namespace ceebase
