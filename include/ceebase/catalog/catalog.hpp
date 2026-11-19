#pragma once

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/common/result.hpp"
#include "ceebase/common/status.hpp"
#include "ceebase/index/b_plus_tree.hpp"
#include "ceebase/record/schema.hpp"
#include "ceebase/record/table_heap.hpp"

namespace ceebase {

struct TableMetadata {
    uint32_t table_id;
    std::string name;
    Schema schema;
    std::unique_ptr<TableHeap> table_heap;
    std::string primary_key_column;
};

struct IndexMetadata {
    uint32_t index_id;
    std::string name;
    std::string table_name;
    std::string key_column_name;
    std::unique_ptr<BPlusTreeIndex> index;
};

class Catalog {
public:
    static constexpr uint32_t CATALOG_MAGIC = 0x43415430; // "CAT0"
    static constexpr uint16_t CATALOG_VERSION = 1;

    explicit Catalog(BufferPoolManager* bpm);
    ~Catalog();

    Catalog(const Catalog&) = delete;
    Catalog& operator=(const Catalog&) = delete;

    Status SaveCatalog();
    Status LoadCatalog();

    // Table Management
    Result<TableMetadata*> CreateTable(std::string name, Schema schema, std::string primary_key = "");
    Result<TableMetadata*> RegisterExistingTable(std::string name, Schema schema, page_id_t first_page_id, std::string primary_key = "");
    [[nodiscard]] TableMetadata* GetTable(std::string_view name) const;
    Status DropTable(std::string_view name);
    [[nodiscard]] std::vector<TableMetadata*> GetAllTables() const;

    // Index Management
    Result<IndexMetadata*> CreateIndex(std::string index_name, std::string table_name, std::string key_column_name);
    [[nodiscard]] IndexMetadata* GetIndex(std::string_view index_name) const;
    [[nodiscard]] IndexMetadata* GetIndexForColumn(std::string_view table_name, std::string_view column_name) const;
    [[nodiscard]] std::vector<IndexMetadata*> GetTableIndexes(std::string_view table_name) const;
    Status DropIndex(std::string_view index_name);

private:
    Status SaveCatalogInternal();
    Status LoadCatalogInternal(page_id_t root_id);

    BufferPoolManager* bpm_;
    uint32_t next_table_id_{1};
    uint32_t next_index_id_{1};
    std::unordered_map<std::string, std::unique_ptr<TableMetadata>> tables_{};
    std::unordered_map<std::string, std::unique_ptr<IndexMetadata>> indexes_{};
    mutable std::mutex latch_{};
};

} // namespace ceebase
