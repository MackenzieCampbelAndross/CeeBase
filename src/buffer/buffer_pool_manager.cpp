#include "ceebase/buffer/buffer_pool_manager.hpp"

#include "ceebase/common/byte_utils.hpp"
#include "ceebase/recovery/log_manager.hpp"

namespace ceebase {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager* disk_manager, LogManager* log_manager)
    : pool_size_(pool_size),
      disk_manager_(disk_manager),
      log_manager_(log_manager),
      replacer_(std::make_unique<LRUReplacer>(pool_size)),
      pages_(pool_size) {
    for (size_t i = 0; i < pool_size_; ++i) {
        free_list_.push_back(static_cast<frame_id_t>(i));
    }
}

BufferPoolManager::~BufferPoolManager() {
    (void)FlushAllPages();
}

bool BufferPoolManager::FindAvailableFrame(frame_id_t* frame_id) {
    if (!free_list_.empty()) {
        *frame_id = free_list_.front();
        free_list_.pop_front();
        return true;
    }

    if (replacer_->Victim(frame_id)) {
        Page& victim = pages_[*frame_id];
        if (victim.IsDirty()) {
            // Enforce Write-Ahead Logging invariant: Page LSN <= Persisted LSN before dirty write
            if (log_manager_ != nullptr && victim.GetLSN() > log_manager_->GetPersistedLSN()) {
                Status log_sync = log_manager_->Flush();
                if (!log_sync.ok()) {
                    return false;
                }
            }
            Status write_status = disk_manager_->WritePage(victim.GetPageId(), victim.GetData());
            if (!write_status.ok()) {
                return false;
            }
            victim.SetDirty(false);
        }
        page_table_.erase(victim.GetPageId());
        return true;
    }

    return false;
}

Result<Page*> BufferPoolManager::FetchPage(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(latch_);

    // 1. Check if page is already resident in buffer pool
    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        pages_[frame_id].pin_count_++;
        replacer_->Pin(frame_id);
        return &pages_[frame_id];
    }

    // 2. Page is not in memory; locate an available frame
    frame_id_t frame_id{INVALID_FRAME_ID};
    if (!FindAvailableFrame(&frame_id)) {
        return Status::BufferPoolExhausted("All buffer pool frames are currently pinned");
    }

    // 3. Read page from disk into the allocated frame
    Page& page = pages_[frame_id];
    page.ResetMemory();
    Status s = disk_manager_->ReadPage(page_id, page.GetData());
    if (!s.ok()) {
        free_list_.push_back(frame_id);
        return s;
    }

    page.page_id_ = page_id;
    page.pin_count_ = 1;
    page.is_dirty_ = false;
    // Extract LSN from page bytes if present
    page.SetLSN(DecodeU64(page.GetData() + 8));

    page_table_[page_id] = frame_id;
    replacer_->Pin(frame_id);

    return &page;
}

Status BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) {
    std::lock_guard<std::mutex> guard(latch_);

    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return Status::NotFound("BufferPoolManager: Page " + std::to_string(page_id) + " not in buffer pool");
    }

    frame_id_t frame_id = it->second;
    Page& page = pages_[frame_id];

    if (is_dirty) {
        page.SetDirty(true);
    }

    if (page.pin_count_ <= 0) {
        return Status::InvalidArgument("BufferPoolManager: Attempting to unpin unpinned page " + std::to_string(page_id));
    }

    page.pin_count_--;
    if (page.pin_count_ == 0) {
        replacer_->Unpin(frame_id);
    }

    return Status::Ok();
}

Status BufferPoolManager::FlushPage(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = page_table_.find(page_id);
    if (it == page_table_.end()) {
        return Status::NotFound("BufferPoolManager: Page not found for flush");
    }

    frame_id_t frame_id = it->second;
    Page& page = pages_[frame_id];
    if (page.IsDirty()) {
        // Enforce Write-Ahead Logging invariant
        if (log_manager_ != nullptr && page.GetLSN() > log_manager_->GetPersistedLSN()) {
            Status log_sync = log_manager_->Flush();
            if (!log_sync.ok()) {
                return log_sync;
            }
        }
        Status s = disk_manager_->WritePage(page.GetPageId(), page.GetData());
        if (!s.ok()) {
            return s;
        }
        page.SetDirty(false);
    }
    return Status::Ok();
}

Status BufferPoolManager::FlushAllPages() {
    std::lock_guard<std::mutex> guard(latch_);
    for (auto& [page_id, frame_id] : page_table_) {
        Page& page = pages_[frame_id];
        if (page.IsDirty()) {
            // Enforce Write-Ahead Logging invariant
            if (log_manager_ != nullptr && page.GetLSN() > log_manager_->GetPersistedLSN()) {
                Status log_sync = log_manager_->Flush();
                if (!log_sync.ok()) {
                    return log_sync;
                }
            }
            Status s = disk_manager_->WritePage(page.GetPageId(), page.GetData());
            if (!s.ok()) {
                return s;
            }
            page.SetDirty(false);
        }
    }
    return Status::Ok();
}

Result<Page*> BufferPoolManager::NewPage(page_id_t* page_id) {
    std::lock_guard<std::mutex> guard(latch_);

    frame_id_t frame_id{INVALID_FRAME_ID};
    if (!FindAvailableFrame(&frame_id)) {
        return Status::BufferPoolExhausted("Buffer pool exhausted: cannot allocate new page");
    }

    auto alloc_res = disk_manager_->AllocatePage();
    if (!alloc_res.ok()) {
        free_list_.push_back(frame_id);
        return alloc_res.status();
    }

    *page_id = *alloc_res;
    Page& page = pages_[frame_id];
    page.ResetMemory();
    page.page_id_ = *page_id;
    page.pin_count_ = 1;
    page.is_dirty_ = true;

    page_table_[*page_id] = frame_id;
    replacer_->Pin(frame_id);

    return &page;
}

Status BufferPoolManager::DeletePage(page_id_t page_id) {
    std::lock_guard<std::mutex> guard(latch_);

    auto it = page_table_.find(page_id);
    if (it != page_table_.end()) {
        frame_id_t frame_id = it->second;
        Page& page = pages_[frame_id];
        if (page.pin_count_ > 0) {
            return Status::InvalidArgument("BufferPoolManager: Cannot delete pinned page " + std::to_string(page_id));
        }

        replacer_->Pin(frame_id);
        page_table_.erase(it);
        page.ResetMemory();
        free_list_.push_back(frame_id);
    }

    return disk_manager_->DeallocatePage(page_id);
}

size_t BufferPoolManager::GetNumFreeFrames() const {
    std::lock_guard<std::mutex> guard(latch_);
    return free_list_.size();
}

bool BufferPoolManager::IsPageInPool(page_id_t page_id) const {
    std::lock_guard<std::mutex> guard(latch_);
    return page_table_.find(page_id) != page_table_.end();
}

} // namespace ceebase
