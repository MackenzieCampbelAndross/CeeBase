#pragma once

#include <list>
#include <mutex>
#include <unordered_map>

#include "ceebase/common/types.hpp"

namespace ceebase {

class LRUReplacer {
public:
    explicit LRUReplacer(size_t num_pages);
    ~LRUReplacer() = default;

    LRUReplacer(const LRUReplacer&) = delete;
    LRUReplacer& operator=(const LRUReplacer&) = delete;

    // Pick the least recently used frame for eviction.
    // Returns true if a victim was found; false if all frames are currently pinned.
    bool Victim(frame_id_t* frame_id);

    // Pin a frame, removing it from the replacer (cannot be evicted).
    void Pin(frame_id_t frame_id);

    // Unpin a frame, adding it to the replacer (eligible for eviction).
    void Unpin(frame_id_t frame_id);

    [[nodiscard]] size_t Size() const;

private:
    size_t capacity_;
    mutable std::mutex latch_{};
    std::list<frame_id_t> lru_list_{};
    std::unordered_map<frame_id_t, std::list<frame_id_t>::iterator> lru_map_{};
};

} // namespace ceebase
