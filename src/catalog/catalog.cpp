#include "ceebase/catalog/catalog.hpp"

namespace ceebase {

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
    return Status::Ok();
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
    for (auto it = heap->Begin(); it != heap->End(); ++it) {
        Value val = it->GetValue(schema, static_cast<size_t>(col_idx));
        int64_t key_val = 0;
        if (val.GetType() == DataType::INTEGER) {
            key_val = val.GetInteger();
        } else if (val.GetType() == DataType::BIGINT) {
            key_val = val.GetBigInt();
        }
        (void)btree->Insert(key_val, it->GetRID());
    }

    auto meta = std::make_unique<IndexMetadata>();
    meta->index_id = next_index_id_++;
    meta->name = index_name;
    meta->table_name = table_name;
    meta->key_column_name = key_column_name;
    meta->index = std::move(btree);

    IndexMetadata* ptr = meta.get();
    indexes_[index_name] = std::move(meta);
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
    return Status::Ok();
}

} // namespace ceebase
