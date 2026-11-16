#include "ceebase/index/b_plus_tree.hpp"

namespace ceebase {

// IndexIterator Implementation
IndexIterator::IndexIterator(BufferPoolManager* bpm, page_id_t leaf_page_id, int index)
    : bpm_(bpm), leaf_page_id_(leaf_page_id), index_(index) {
    if (leaf_page_id_ != INVALID_PAGE_ID) {
        auto res = bpm_->FetchPage(leaf_page_id_);
        if (res.ok()) {
            page_ = *res;
        } else {
            leaf_page_id_ = INVALID_PAGE_ID;
            page_ = nullptr;
        }
    }
}

IndexIterator::~IndexIterator() {
    if (page_ != nullptr) {
        (void)bpm_->UnpinPage(leaf_page_id_, false);
        page_ = nullptr;
    }
}

IndexIterator::IndexIterator(IndexIterator&& other) noexcept
    : bpm_(other.bpm_), leaf_page_id_(other.leaf_page_id_), index_(other.index_), page_(other.page_) {
    other.bpm_ = nullptr;
    other.leaf_page_id_ = INVALID_PAGE_ID;
    other.index_ = 0;
    other.page_ = nullptr;
}

IndexIterator& IndexIterator::operator=(IndexIterator&& other) noexcept {
    if (this != &other) {
        if (page_ != nullptr) {
            (void)bpm_->UnpinPage(leaf_page_id_, false);
        }
        bpm_ = other.bpm_;
        leaf_page_id_ = other.leaf_page_id_;
        index_ = other.index_;
        page_ = other.page_;
        other.bpm_ = nullptr;
        other.leaf_page_id_ = INVALID_PAGE_ID;
        other.index_ = 0;
        other.page_ = nullptr;
    }
    return *this;
}

bool IndexIterator::operator==(const IndexIterator& other) const noexcept {
    return leaf_page_id_ == other.leaf_page_id_ && index_ == other.index_;
}

IndexIterator& IndexIterator::operator++() {
    if (leaf_page_id_ == INVALID_PAGE_ID || page_ == nullptr) {
        return *this;
    }

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(page_->GetData());
    index_++;

    if (index_ >= leaf->GetSize()) {
        page_id_t next_page_id = leaf->GetNextPageId();
        (void)bpm_->UnpinPage(leaf_page_id_, false);
        page_ = nullptr;
        leaf_page_id_ = next_page_id;
        index_ = 0;

        if (leaf_page_id_ != INVALID_PAGE_ID) {
            auto res = bpm_->FetchPage(leaf_page_id_);
            if (res.ok()) {
                page_ = *res;
            } else {
                leaf_page_id_ = INVALID_PAGE_ID;
            }
        }
    }
    return *this;
}

std::pair<int64_t, RID> IndexIterator::operator*() const {
    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(page_->GetData());
    return {leaf->KeyAt(index_), leaf->ValueAt(index_)};
}

// BPlusTreeIndex Implementation
BPlusTreeIndex::BPlusTreeIndex(std::string name, BufferPoolManager* bpm, page_id_t root_page_id,
                               int leaf_max_size, int internal_max_size)
    : name_(std::move(name)),
      bpm_(bpm),
      root_page_id_(root_page_id),
      leaf_max_size_(leaf_max_size),
      internal_max_size_(internal_max_size) {}

Page* BPlusTreeIndex::FindLeafPage(int64_t key, bool left_most) {
    if (IsEmpty()) {
        return nullptr;
    }

    auto cur_res = bpm_->FetchPage(root_page_id_);
    if (!cur_res.ok()) {
        return nullptr;
    }
    Page* cur_page = *cur_res;
    auto* btree_page = reinterpret_cast<BPlusTreePage*>(cur_page->GetData());

    while (!btree_page->IsLeafPage()) {
        auto* internal = reinterpret_cast<BPlusTreeInternalPage*>(btree_page);
        page_id_t next_pid = left_most ? internal->ValueAt(0) : internal->Lookup(key);

        auto next_res = bpm_->FetchPage(next_pid);
        if (!next_res.ok()) {
            (void)bpm_->UnpinPage(cur_page->GetPageId(), false);
            return nullptr;
        }

        (void)bpm_->UnpinPage(cur_page->GetPageId(), false);
        cur_page = *next_res;
        btree_page = reinterpret_cast<BPlusTreePage*>(cur_page->GetData());
    }

    return cur_page;
}

bool BPlusTreeIndex::GetValue(int64_t key, RID* result) {
    std::lock_guard<std::mutex> guard(latch_);
    Page* leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return false;
    }

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    bool found = leaf->Lookup(key, result);
    (void)bpm_->UnpinPage(leaf_page->GetPageId(), false);
    return found;
}

Status BPlusTreeIndex::Insert(int64_t key, const RID& value) {
    std::lock_guard<std::mutex> guard(latch_);
    if (IsEmpty()) {
        StartNewTree(key, value);
        return Status::Ok();
    }

    Page* leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return Status::Internal("BPlusTree: Failed locating leaf page");
    }

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    InsertIntoLeaf(leaf, key, value);
    (void)bpm_->UnpinPage(leaf_page->GetPageId(), true);
    return Status::Ok();
}

void BPlusTreeIndex::StartNewTree(int64_t key, const RID& value) {
    auto res = bpm_->NewPage(&root_page_id_);
    if (!res.ok()) {
        return;
    }
    Page* root_page = *res;
    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(root_page->GetData());
    leaf->Init(root_page_id_, INVALID_PAGE_ID, leaf_max_size_);
    leaf->Insert(key, value);
    (void)bpm_->UnpinPage(root_page_id_, true);
}

void BPlusTreeIndex::InsertIntoLeaf(BPlusTreeLeafPage* leaf, int64_t key, const RID& value) {
    leaf->Insert(key, value);

    if (leaf->GetSize() > leaf->GetMaxSize()) {
        // Leaf overflow: Split leaf page
        page_id_t new_page_id = INVALID_PAGE_ID;
        auto res = bpm_->NewPage(&new_page_id);
        if (!res.ok()) {
            return;
        }

        Page* new_page = *res;
        auto* new_leaf = reinterpret_cast<BPlusTreeLeafPage*>(new_page->GetData());
        new_leaf->Init(new_page_id, leaf->GetParentPageId(), leaf_max_size_);

        leaf->MoveHalfTo(new_leaf);

        new_leaf->SetNextPageId(leaf->GetNextPageId());
        new_leaf->SetPrevPageId(leaf->GetPageId());

        if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
            auto next_res = bpm_->FetchPage(leaf->GetNextPageId());
            if (next_res.ok()) {
                auto* next_leaf = reinterpret_cast<BPlusTreeLeafPage*>((*next_res)->GetData());
                next_leaf->SetPrevPageId(new_page_id);
                (void)bpm_->UnpinPage(leaf->GetNextPageId(), true);
            }
        }
        leaf->SetNextPageId(new_page_id);

        InsertIntoParent(leaf, new_leaf->KeyAt(0), new_leaf);
        (void)bpm_->UnpinPage(new_page_id, true);
    }
}

void BPlusTreeIndex::InsertIntoParent(BPlusTreePage* old_node, int64_t key, BPlusTreePage* new_node) {
    if (old_node->IsRootPage()) {
        page_id_t new_root_id = INVALID_PAGE_ID;
        auto res = bpm_->NewPage(&new_root_id);
        if (!res.ok()) {
            return;
        }

        Page* new_root_page = *res;
        auto* new_root = reinterpret_cast<BPlusTreeInternalPage*>(new_root_page->GetData());
        new_root->Init(new_root_id, INVALID_PAGE_ID, internal_max_size_);
        new_root->Populate(old_node->GetPageId(), key, new_node->GetPageId());

        old_node->SetParentPageId(new_root_id);
        new_node->SetParentPageId(new_root_id);
        root_page_id_ = new_root_id;

        (void)bpm_->UnpinPage(new_root_id, true);
        return;
    }

    page_id_t parent_id = old_node->GetParentPageId();
    auto parent_res = bpm_->FetchPage(parent_id);
    if (!parent_res.ok()) {
        return;
    }

    Page* parent_page = *parent_res;
    auto* parent = reinterpret_cast<BPlusTreeInternalPage*>(parent_page->GetData());
    parent->InsertNodeAfter(old_node->GetPageId(), key, new_node->GetPageId());
    new_node->SetParentPageId(parent_id);

    if (parent->GetSize() > parent->GetMaxSize()) {
        // Internal node overflow: Split internal page
        page_id_t new_parent_id = INVALID_PAGE_ID;
        auto new_parent_res = bpm_->NewPage(&new_parent_id);
        if (new_parent_res.ok()) {
            Page* new_parent_page = *new_parent_res;
            auto* new_parent = reinterpret_cast<BPlusTreeInternalPage*>(new_parent_page->GetData());
            new_parent->Init(new_parent_id, parent->GetParentPageId(), internal_max_size_);

            parent->MoveHalfTo(new_parent, bpm_);
            int64_t middle_key = new_parent->KeyAt(0);

            InsertIntoParent(parent, middle_key, new_parent);
            (void)bpm_->UnpinPage(new_parent_id, true);
        }
    }

    (void)bpm_->UnpinPage(parent_id, true);
}

Status BPlusTreeIndex::Remove(int64_t key) {
    std::lock_guard<std::mutex> guard(latch_);
    if (IsEmpty()) {
        return Status::NotFound("BPlusTree is empty");
    }

    Page* leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return Status::NotFound("Key not found in BPlusTree");
    }

    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    int old_size = leaf->GetSize();
    int new_size = leaf->RemoveAndDeleteRecord(key);

    if (old_size == new_size) {
        (void)bpm_->UnpinPage(leaf_page->GetPageId(), false);
        return Status::NotFound("Key not found in leaf page");
    }

    page_id_t leaf_pid = leaf_page->GetPageId();
    bool node_deleted = false;
    if (leaf->GetSize() < leaf->GetMinSize()) {
        node_deleted = CoalesceOrRedistribute(leaf);
    }

    if (!node_deleted) {
        (void)bpm_->UnpinPage(leaf_pid, true);
    }
    return Status::Ok();
}

bool BPlusTreeIndex::CoalesceOrRedistribute(BPlusTreePage* node) {
    if (node->IsRootPage()) {
        return AdjustRoot(node);
    }

    page_id_t parent_pid = node->GetParentPageId();
    auto parent_res = bpm_->FetchPage(parent_pid);
    if (!parent_res.ok()) {
        return false;
    }
    Page* parent_page = *parent_res;
    auto* parent = reinterpret_cast<BPlusTreeInternalPage*>(parent_page->GetData());
    int node_idx = parent->ValueIndex(node->GetPageId());
    if (node_idx == -1) {
        (void)bpm_->UnpinPage(parent_pid, false);
        return false;
    }

    int neighbor_idx = (node_idx == 0) ? 1 : node_idx - 1;
    page_id_t neighbor_pid = parent->ValueAt(neighbor_idx);

    auto neighbor_res = bpm_->FetchPage(neighbor_pid);
    if (!neighbor_res.ok()) {
        (void)bpm_->UnpinPage(parent_pid, false);
        return false;
    }
    Page* neighbor_page = *neighbor_res;
    auto* neighbor = reinterpret_cast<BPlusTreePage*>(neighbor_page->GetData());

    bool node_deleted = false;

    if (node->GetSize() + neighbor->GetSize() <= node->GetMaxSize()) {
        // Coalesce
        if (node_idx == 0) {
            // node is index 0 (left), neighbor is index 1 (right).
            // Move neighbor into node. neighbor is deleted!
            Coalesce(node, neighbor, parent, neighbor_idx);
            (void)bpm_->UnpinPage(parent_pid, true);
            (void)bpm_->UnpinPage(neighbor_pid, true);
            (void)bpm_->DeletePage(neighbor_pid);
            node_deleted = false;
        } else {
            // neighbor is left, node is right.
            // Move node into neighbor. node is deleted!
            Coalesce(neighbor, node, parent, node_idx);
            (void)bpm_->UnpinPage(parent_pid, true);
            (void)bpm_->UnpinPage(neighbor_pid, true);
            (void)bpm_->UnpinPage(node->GetPageId(), true);
            (void)bpm_->DeletePage(node->GetPageId());
            node_deleted = true;
        }
    } else {
        // Redistribute
        Redistribute(neighbor, node, parent, node_idx);
        (void)bpm_->UnpinPage(parent_pid, true);
        (void)bpm_->UnpinPage(neighbor_pid, true);
        node_deleted = false;
    }

    return node_deleted;
}

bool BPlusTreeIndex::Coalesce(BPlusTreePage* neighbor_node, BPlusTreePage* node,
                             BPlusTreeInternalPage* parent, int index) {
    if (node->IsLeafPage()) {
        auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(node);
        auto* neighbor_leaf = reinterpret_cast<BPlusTreeLeafPage*>(neighbor_node);
        leaf->MoveAllTo(neighbor_leaf);
        if (leaf->GetNextPageId() != INVALID_PAGE_ID) {
            auto next_res = bpm_->FetchPage(leaf->GetNextPageId());
            if (next_res.ok()) {
                auto* next_leaf = reinterpret_cast<BPlusTreeLeafPage*>((*next_res)->GetData());
                next_leaf->SetPrevPageId(neighbor_leaf->GetPageId());
                (void)bpm_->UnpinPage(leaf->GetNextPageId(), true);
            }
        }
    } else {
        auto* internal = reinterpret_cast<BPlusTreeInternalPage*>(node);
        auto* neighbor_internal = reinterpret_cast<BPlusTreeInternalPage*>(neighbor_node);
        internal->MoveAllTo(neighbor_internal, parent->KeyAt(index), bpm_);
    }

    parent->Remove(index);

    if (parent->GetSize() < parent->GetMinSize()) {
        CoalesceOrRedistribute(parent);
    }
    return true;
}

void BPlusTreeIndex::Redistribute(BPlusTreePage* neighbor_node, BPlusTreePage* node,
                                 BPlusTreeInternalPage* parent, int index) {
    if (node->IsLeafPage()) {
        auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(node);
        auto* neighbor_leaf = reinterpret_cast<BPlusTreeLeafPage*>(neighbor_node);
        if (index == 0) {
            neighbor_leaf->MoveFirstToEndOf(leaf);
            parent->SetKeyAt(1, neighbor_leaf->KeyAt(0));
        } else {
            neighbor_leaf->MoveLastToFrontOf(leaf);
            parent->SetKeyAt(index, leaf->KeyAt(0));
        }
    } else {
        auto* internal = reinterpret_cast<BPlusTreeInternalPage*>(node);
        auto* neighbor_internal = reinterpret_cast<BPlusTreeInternalPage*>(neighbor_node);
        if (index == 0) {
            int64_t next_key = neighbor_internal->KeyAt(1);
            neighbor_internal->MoveFirstToEndOf(internal, parent->KeyAt(1), bpm_);
            parent->SetKeyAt(1, next_key);
        } else {
            int64_t last_key = neighbor_internal->KeyAt(neighbor_internal->GetSize() - 1);
            neighbor_internal->MoveLastToFrontOf(internal, parent->KeyAt(index), bpm_);
            parent->SetKeyAt(index, last_key);
        }
    }
}

bool BPlusTreeIndex::AdjustRoot(BPlusTreePage* old_root_node) {
    if (old_root_node->IsLeafPage()) {
        if (old_root_node->GetSize() == 0) {
            root_page_id_ = INVALID_PAGE_ID;
            (void)bpm_->DeletePage(old_root_node->GetPageId());
            return true;
        }
        return false;
    }

    auto* internal = reinterpret_cast<BPlusTreeInternalPage*>(old_root_node);
    if (internal->GetSize() == 1) {
        page_id_t new_root_id = internal->ValueAt(0);
        auto child_res = bpm_->FetchPage(new_root_id);
        if (child_res.ok()) {
            auto* child = reinterpret_cast<BPlusTreePage*>((*child_res)->GetData());
            child->SetParentPageId(INVALID_PAGE_ID);
            root_page_id_ = new_root_id;
            (void)bpm_->UnpinPage(new_root_id, true);
            (void)bpm_->DeletePage(old_root_node->GetPageId());
            return true;
        }
    }
    return false;
}

IndexIterator BPlusTreeIndex::Begin() {
    std::lock_guard<std::mutex> guard(latch_);
    Page* leaf_page = FindLeafPage(0, true);
    if (leaf_page == nullptr) {
        return End();
    }
    page_id_t pid = leaf_page->GetPageId();
    (void)bpm_->UnpinPage(pid, false);
    return IndexIterator(bpm_, pid, 0);
}

IndexIterator BPlusTreeIndex::Begin(int64_t key) {
    std::lock_guard<std::mutex> guard(latch_);
    Page* leaf_page = FindLeafPage(key);
    if (leaf_page == nullptr) {
        return End();
    }
    auto* leaf = reinterpret_cast<BPlusTreeLeafPage*>(leaf_page->GetData());
    int idx = leaf->KeyIndex(key);
    page_id_t pid = leaf_page->GetPageId();
    (void)bpm_->UnpinPage(pid, false);
    return IndexIterator(bpm_, pid, idx);
}

IndexIterator BPlusTreeIndex::End() {
    return IndexIterator(bpm_, INVALID_PAGE_ID, 0);
}

size_t BPlusTreeIndex::GetHeight() {
    std::lock_guard<std::mutex> guard(latch_);
    if (IsEmpty()) {
        return 0;
    }
    size_t height = 0;
    page_id_t cur_id = root_page_id_;
    while (cur_id != INVALID_PAGE_ID) {
        height++;
        auto res = bpm_->FetchPage(cur_id);
        if (!res.ok()) {
            break;
        }
        Page* page = *res;
        auto* btree_page = reinterpret_cast<BPlusTreePage*>(page->GetData());
        if (btree_page->IsLeafPage()) {
            (void)bpm_->UnpinPage(cur_id, false);
            break;
        }
        auto* internal = reinterpret_cast<BPlusTreeInternalPage*>(btree_page);
        page_id_t next_id = internal->ValueAt(0);
        (void)bpm_->UnpinPage(cur_id, false);
        cur_id = next_id;
    }
    return height;
}

} // namespace ceebase
