#include "ceebase/catalog/catalog.hpp"

#include <cstring>
#include <span>

#include "ceebase/common/byte_utils.hpp"
#include "ceebase/common/config.hpp"

namespace ceebase {

Catalog::Catalog(BufferPoolManager* bpm) : bpm_(bpm) {
    if (bpm_) {
        page_id_t root_id = bpm_->GetCatalogRootPageId();
        if (root_id != INVALID_PAGE_ID) {
            (void)LoadCatalog();
        }
    }
}

Catalog::~Catalog() {
    try {
        if (bpm_) {
            (void)SaveCatalog();
        }
    } catch (const std::exception& e) {
        // Suppress exception in destructor
    }
}

Status Catalog::SaveCatalog() {
    std::lock_guard<std::mutex> guard(latch_);
    return SaveCatalogInternal();
}

Status Catalog::LoadCatalog() {
    std::lock_guard<std::mutex> guard(latch_);
    if (!bpm_) {
        return Status::InvalidArgument("Catalog: BufferPoolManager is null");
    }
    page_id_t root_id = bpm_->GetCatalogRootPageId();
    if (root_id == INVALID_PAGE_ID) {
        return Status::Ok();
    }
    return LoadCatalogInternal(root_id);
}

Status Catalog::SaveCatalogInternal() {
    if (!bpm_) {
        return Status::Ok();
    }

    page_id_t root_id = bpm_->GetCatalogRootPageId();
    if (root_id == INVALID_PAGE_ID) {
        auto page_res = bpm_->NewPage(&root_id);
        if (!page_res.ok()) {
            return page_res.status();
        }
        Status s = bpm_->SetCatalogRootPageId(root_id);
        if (!s.ok()) {
            (void)bpm_->UnpinPage(root_id, false);
            return s;
        }
        (void)bpm_->UnpinPage(root_id, false);
    }

    auto fetch_res = bpm_->FetchPage(root_id);
    if (!fetch_res.ok()) {
        return fetch_res.status();
    }
    Page* cat_page = *fetch_res;
    uint8_t* data = cat_page->GetData();
    std::fill_n(data, PAGE_SIZE, uint8_t{0});

    ByteWriter writer(std::span<uint8_t>(data, PAGE_SIZE - 4));

    // Catalog Page Header fields
    (void)writer.WriteU32(CATALOG_MAGIC);
    (void)writer.WriteU16(CATALOG_VERSION);
    (void)writer.WriteU16(0); // flags
    (void)writer.WriteU32(INVALID_PAGE_ID); // next_catalog_page_id
    (void)writer.WriteU32(static_cast<uint32_t>(tables_.size()));
    (void)writer.WriteU32(static_cast<uint32_t>(indexes_.size()));
    (void)writer.WriteU32(next_table_id_);
    (void)writer.WriteU32(next_index_id_);

    // Serialize Tables
    for (const auto& [name, tbl] : tables_) {
        (void)writer.WriteU32(tbl->table_id);
        page_id_t first_p = tbl->table_heap ? tbl->table_heap->GetFirstPageId() : INVALID_PAGE_ID;
        (void)writer.WriteU32(first_p);
        (void)writer.WriteString(tbl->name);
        (void)writer.WriteString(tbl->primary_key_column);

        const auto& cols = tbl->schema.GetColumns();
        (void)writer.WriteU16(static_cast<uint16_t>(cols.size()));
        for (const auto& col : cols) {
            (void)writer.WriteString(col.GetName());
            (void)writer.WriteU8(static_cast<uint8_t>(col.GetType()));
            (void)writer.WriteU8(col.IsNullable() ? 1 : 0);
            (void)writer.WriteU8((col.GetName() == tbl->primary_key_column) ? 1 : 0);
        }
    }

    // Serialize Indexes
    for (const auto& [idx_name, idx] : indexes_) {
        (void)writer.WriteU32(idx->index_id);
        page_id_t idx_root = idx->index ? idx->index->GetRootPageId() : INVALID_PAGE_ID;
        (void)writer.WriteU32(idx_root);
        (void)writer.WriteString(idx->name);
        (void)writer.WriteString(idx->table_name);
        (void)writer.WriteString(idx->key_column_name);
    }

    // Compute and append CRC32 checksum at [4092..4095]
    uint32_t checksum = ComputeCRC32(std::span<const uint8_t>(data, PAGE_SIZE - 4));
    EncodeU32(checksum, data + (PAGE_SIZE - 4));

    (void)bpm_->UnpinPage(root_id, true);
    return Status::Ok();
}

Status Catalog::LoadCatalogInternal(page_id_t root_id) {
    auto fetch_res = bpm_->FetchPage(root_id);
    if (!fetch_res.ok()) {
        return fetch_res.status();
    }
    Page* cat_page = *fetch_res;
    const uint8_t* data = cat_page->GetData();

    // Verify CRC32
    uint32_t expected_crc = DecodeU32(data + (PAGE_SIZE - 4));
    uint32_t computed_crc = ComputeCRC32(std::span<const uint8_t>(data, PAGE_SIZE - 4));
    if (expected_crc != computed_crc) {
        (void)bpm_->UnpinPage(root_id, false);
        return Status::Corruption("Catalog root page CRC32 checksum mismatch");
    }

    ByteReader reader(std::span<const uint8_t>(data, PAGE_SIZE - 4));
    auto magic_res = reader.ReadU32();
    if (!magic_res.ok() || *magic_res != CATALOG_MAGIC) {
        (void)bpm_->UnpinPage(root_id, false);
        return Status::Corruption("Invalid catalog page magic number");
    }

    auto version_res = reader.ReadU16();
    if (!version_res.ok() || *version_res != CATALOG_VERSION) {
        (void)bpm_->UnpinPage(root_id, false);
        return Status::Corruption("Unsupported catalog page version");
    }

    auto flags_res = reader.ReadU16();
    auto next_page_res = reader.ReadU32();
    auto tbl_count_res = reader.ReadU32();
    auto idx_count_res = reader.ReadU32();
    auto next_tbl_id_res = reader.ReadU32();
    auto next_idx_id_res = reader.ReadU32();

    if (!tbl_count_res.ok() || !idx_count_res.ok() || !next_tbl_id_res.ok() || !next_idx_id_res.ok()) {
        (void)bpm_->UnpinPage(root_id, false);
        return Status::Corruption("Catalog header fields truncated");
    }

    next_table_id_ = *next_tbl_id_res;
    next_index_id_ = *next_idx_id_res;
    uint32_t table_count = *tbl_count_res;
    uint32_t index_count = *idx_count_res;

    tables_.clear();
    indexes_.clear();

    // Decode Tables
    for (uint32_t i = 0; i < table_count; ++i) {
        auto tbl_id = reader.ReadU32();
        auto first_page = reader.ReadU32();
        auto name = reader.ReadString();
        auto pk_col = reader.ReadString();
        auto col_count = reader.ReadU16();
        if (!tbl_id.ok() || !first_page.ok() || !name.ok() || !pk_col.ok() || !col_count.ok()) {
            (void)bpm_->UnpinPage(root_id, false);
            return Status::Corruption("Corrupt table entry in catalog");
        }

        std::vector<Column> cols;
        cols.reserve(*col_count);
        for (uint16_t c = 0; c < *col_count; ++c) {
            auto col_name = reader.ReadString();
            auto col_type = reader.ReadU8();
            auto col_nullable = reader.ReadU8();
            auto col_pk = reader.ReadU8();
            if (!col_name.ok() || !col_type.ok() || !col_nullable.ok() || !col_pk.ok()) {
                (void)bpm_->UnpinPage(root_id, false);
                return Status::Corruption("Corrupt column entry in catalog");
            }
            cols.emplace_back(*col_name, static_cast<DataType>(*col_type), (*col_nullable != 0));
        }

        Schema schema(std::move(cols));
        auto meta = std::make_unique<TableMetadata>();
        meta->table_id = *tbl_id;
        meta->name = *name;
        meta->schema = std::move(schema);
        meta->primary_key_column = *pk_col;
        if (*first_page != INVALID_PAGE_ID) {
            meta->table_heap = TableHeap::Open(bpm_, *first_page);
        }
        tables_[meta->name] = std::move(meta);
    }

    // Decode Indexes
    for (uint32_t i = 0; i < index_count; ++i) {
        auto idx_id = reader.ReadU32();
        auto root_page = reader.ReadU32();
        auto idx_name = reader.ReadString();
        auto tbl_name = reader.ReadString();
        auto key_col = reader.ReadString();
        if (!idx_id.ok() || !root_page.ok() || !idx_name.ok() || !tbl_name.ok() || !key_col.ok()) {
            (void)bpm_->UnpinPage(root_id, false);
            return Status::Corruption("Corrupt index entry in catalog");
        }

        auto idx_meta = std::make_unique<IndexMetadata>();
        idx_meta->index_id = *idx_id;
        idx_meta->name = *idx_name;
        idx_meta->table_name = *tbl_name;
        idx_meta->key_column_name = *key_col;
        idx_meta->index = std::make_unique<BPlusTreeIndex>(*idx_name, bpm_, *root_page);
        indexes_[idx_meta->name] = std::move(idx_meta);
    }

    (void)bpm_->UnpinPage(root_id, false);
    return Status::Ok();
}

Result<TableMetadata*> Catalog::CreateTable(std::string name, Schema schema, std::string primary_key) {
    std::lock_guard<std::mutex> guard(latch_);
    if (tables_.find(name) != tables_.end()) {
        return Status::AlreadyExists("Table already exists: " + name);
    }

    if (!primary_key.empty() && schema.GetColumnIndex(primary_key) == -1) {
        return Status::InvalidArgument("Primary key column '" + primary_key + "' not in schema");
    }

    auto heap_res = TableHeap::Create(bpm_);
    if (!heap_res.ok()) {
        return heap_res.status();
    }

    auto meta = std::make_unique<TableMetadata>();
    meta->table_id = next_table_id_++;
    meta->name = name;
    meta->schema = std::move(schema);
    meta->table_heap = std::move(*heap_res);
    meta->primary_key_column = std::move(primary_key);

    TableMetadata* ptr = meta.get();
    tables_[name] = std::move(meta);

    // If primary key specified, automatically create a primary key index
    if (!ptr->primary_key_column.empty()) {
        std::string pk_index_name = name + "_pk_idx";
        auto index = std::make_unique<BPlusTreeIndex>(pk_index_name, bpm_);
        auto idx_meta = std::make_unique<IndexMetadata>();
        idx_meta->index_id = next_index_id_++;
        idx_meta->name = pk_index_name;
        idx_meta->table_name = name;
        idx_meta->key_column_name = ptr->primary_key_column;
        idx_meta->index = std::move(index);
        indexes_[pk_index_name] = std::move(idx_meta);
    }

    Status save_status = SaveCatalogInternal();
    if (!save_status.ok()) {
        return save_status;
    }

    return ptr;
}

Result<TableMetadata*> Catalog::RegisterExistingTable(std::string name, Schema schema, page_id_t first_page_id, std::string primary_key) {
    std::lock_guard<std::mutex> guard(latch_);
    if (tables_.find(name) != tables_.end()) {
        return Status::AlreadyExists("Table already exists: " + name);
    }

    if (!primary_key.empty() && schema.GetColumnIndex(primary_key) == -1) {
        return Status::InvalidArgument("Primary key column '" + primary_key + "' not in schema");
    }

    auto meta = std::make_unique<TableMetadata>();
    meta->table_id = next_table_id_++;
    meta->name = name;
    meta->schema = std::move(schema);
    meta->table_heap = TableHeap::Open(bpm_, first_page_id);
    meta->primary_key_column = std::move(primary_key);

    TableMetadata* ptr = meta.get();
    tables_[name] = std::move(meta);

    if (!ptr->primary_key_column.empty()) {
        std::string pk_index_name = name + "_pk_idx";
        auto index = std::make_unique<BPlusTreeIndex>(pk_index_name, bpm_);
        auto idx_meta = std::make_unique<IndexMetadata>();
        idx_meta->index_id = next_index_id_++;
        idx_meta->name = pk_index_name;
        idx_meta->table_name = name;
        idx_meta->key_column_name = ptr->primary_key_column;
        idx_meta->index = std::move(index);
        indexes_[pk_index_name] = std::move(idx_meta);
    }

    Status save_status = SaveCatalogInternal();
    if (!save_status.ok()) {
        return save_status;
    }

    return ptr;
}

TableMetadata* Catalog::GetTable(std::string_view name) const {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = tables_.find(std::string(name));
    if (it == tables_.end()) {
        return nullptr;
    }
    return it->second.get();
}

Status Catalog::DropTable(std::string_view name) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = tables_.find(std::string(name));
    if (it == tables_.end()) {
        return Status::NotFound("Table not found: " + std::string(name));
    }

    // Drop all associated indexes
    std::vector<std::string> idx_to_drop;
    for (const auto& [idx_name, idx_meta] : indexes_) {
        if (idx_meta->table_name == name) {
            idx_to_drop.push_back(idx_name);
        }
    }
    for (const auto& idx_name : idx_to_drop) {
        indexes_.erase(idx_name);
    }

    tables_.erase(it);
    return SaveCatalogInternal();
}

std::vector<TableMetadata*> Catalog::GetAllTables() const {
    std::lock_guard<std::mutex> guard(latch_);
    std::vector<TableMetadata*> result;
    result.reserve(tables_.size());
    for (const auto& [_, meta] : tables_) {
        result.push_back(meta.get());
    }
    return result;
}

Result<IndexMetadata*> Catalog::CreateIndex(std::string index_name, std::string table_name, std::string key_column_name) {
    std::lock_guard<std::mutex> guard(latch_);
    if (indexes_.find(index_name) != indexes_.end()) {
        return Status::AlreadyExists("Index already exists: " + index_name);
    }

    auto tbl_it = tables_.find(table_name);
    if (tbl_it == tables_.end()) {
        return Status::NotFound("Table not found for index: " + table_name);
    }

    const auto& schema = tbl_it->second->schema;
    int col_idx = schema.GetColumnIndex(key_column_name);
    if (col_idx == -1) {
        return Status::InvalidArgument("Index column '" + key_column_name + "' does not exist in table '" + table_name + "'");
    }

    auto btree = std::make_unique<BPlusTreeIndex>(index_name, bpm_);

    // Populate index with existing table records
    auto* heap = tbl_it->second->table_heap.get();
    if (heap) {
        for (auto it = heap->Begin(); it != heap->End(); ++it) {
            Value val = it->GetValue(schema, static_cast<size_t>(col_idx));
            int64_t key_val = 0;
            if (val.GetType() == DataType::INTEGER) {
                key_val = val.GetInteger();
            } else if (val.GetType() == DataType::BIGINT) {
                key_val = val.GetBigInt();
            }
            Status ins_s = btree->Insert(key_val, it->GetRID());
            if (!ins_s.ok()) {
                return ins_s;
            }
        }
    }

    auto meta = std::make_unique<IndexMetadata>();
    meta->index_id = next_index_id_++;
    meta->name = index_name;
    meta->table_name = table_name;
    meta->key_column_name = key_column_name;
    meta->index = std::move(btree);

    IndexMetadata* ptr = meta.get();
    indexes_[index_name] = std::move(meta);

    Status save_status = SaveCatalogInternal();
    if (!save_status.ok()) {
        return save_status;
    }

    return ptr;
}

IndexMetadata* Catalog::GetIndex(std::string_view index_name) const {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = indexes_.find(std::string(index_name));
    if (it == indexes_.end()) {
        return nullptr;
    }
    return it->second.get();
}

IndexMetadata* Catalog::GetIndexForColumn(std::string_view table_name, std::string_view column_name) const {
    std::lock_guard<std::mutex> guard(latch_);
    for (const auto& [_, meta] : indexes_) {
        if (meta->table_name == table_name && meta->key_column_name == column_name) {
            return meta.get();
        }
    }
    return nullptr;
}

std::vector<IndexMetadata*> Catalog::GetTableIndexes(std::string_view table_name) const {
    std::lock_guard<std::mutex> guard(latch_);
    std::vector<IndexMetadata*> result;
    for (const auto& [_, meta] : indexes_) {
        if (meta->table_name == table_name) {
            result.push_back(meta.get());
        }
    }
    return result;
}

Status Catalog::DropIndex(std::string_view index_name) {
    std::lock_guard<std::mutex> guard(latch_);
    auto it = indexes_.find(std::string(index_name));
    if (it == indexes_.end()) {
        return Status::NotFound("Index not found: " + std::string(index_name));
    }
    indexes_.erase(it);
    return SaveCatalogInternal();
}

} // namespace ceebase
