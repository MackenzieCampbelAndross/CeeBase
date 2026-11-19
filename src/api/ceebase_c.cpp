#include "ceebase/ceebase_c.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/common/config.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/execution_engine.hpp"
#include "ceebase/recovery/log_manager.hpp"
#include "ceebase/recovery/recovery_engine.hpp"
#include "ceebase/storage/disk_manager.hpp"

struct ceebase_db {
    std::filesystem::path db_path;
    std::unique_ptr<ceebase::DiskManager> disk_mgr;
    std::unique_ptr<ceebase::LogManager> log_mgr;
    std::unique_ptr<ceebase::BufferPoolManager> bpm;
    std::unique_ptr<ceebase::Catalog> catalog;
    std::unique_ptr<ceebase::TransactionManager> txn_mgr;
    std::unique_ptr<ceebase::RecoveryEngine> recovery;
    std::unique_ptr<ceebase::ExecutionEngine> engine;
    std::string last_error;
    int last_changes{0};
};

struct BoundParam {
    enum class Type { NONE, INT, INT64, DOUBLE, TEXT, NULL_VAL };
    Type type{Type::NONE};
    int32_t int_val{0};
    int64_t int64_val{0};
    double double_val{0.0};
    std::string text_val{};
};

struct ceebase_stmt {
    ceebase_db* db{nullptr};
    std::string sql;
    std::unordered_map<int, BoundParam> bound_params;
    ceebase::QueryResult result;
    size_t current_row{0};
    bool executed{false};
};

extern "C" {

CEEBASE_API int ceebase_open(const char* filename, ceebase_db** db_out) {
    if (!filename || !db_out) {
        return CEEBASE_ERROR;
    }

    *db_out = nullptr;
    auto db = std::make_unique<ceebase_db>();
    db->db_path = filename;
    if (db->db_path.extension() != ".cbdb" && db->db_path.extension() != ".nxdb") {
        db->db_path += ".cbdb";
    }

    db->disk_mgr = std::make_unique<ceebase::DiskManager>();

    // Auto-create database if it does not exist, otherwise open it
    if (!std::filesystem::exists(db->db_path)) {
        auto cs = db->disk_mgr->CreateDatabase(db->db_path);
        if (!cs.ok()) {
            db->last_error = cs.ToString();
            return CEEBASE_ERROR;
        }
    } else {
        auto open_status = db->disk_mgr->OpenDatabase(db->db_path);
        if (!open_status.ok()) {
            db->last_error = open_status.ToString();
            return CEEBASE_ERROR;
        }
    }

    auto wal_path = db->db_path;
    wal_path.replace_extension(".wal");

    db->log_mgr = std::make_unique<ceebase::LogManager>(wal_path);
    db->bpm = std::make_unique<ceebase::BufferPoolManager>(ceebase::DEFAULT_BUFFER_POOL_SIZE,
                                                           db->disk_mgr.get(),
                                                           db->log_mgr.get());
    db->catalog = std::make_unique<ceebase::Catalog>(db->bpm.get());
    db->txn_mgr = std::make_unique<ceebase::TransactionManager>(db->log_mgr.get(),
                                                                db->bpm.get(),
                                                                db->catalog.get());
    db->recovery = std::make_unique<ceebase::RecoveryEngine>(db->log_mgr.get(),
                                                             db->bpm.get(),
                                                             db->catalog.get());

    // Run crash recovery
    auto rec_status = db->recovery->Recover();
    if (!rec_status.ok()) {
        db->last_error = rec_status.ToString();
        if (db->disk_mgr) {
            db->disk_mgr->Close();
        }
        return CEEBASE_ERROR;
    }

    db->engine = std::make_unique<ceebase::ExecutionEngine>(db->bpm.get(),
                                                            db->catalog.get(),
                                                            db->txn_mgr.get(),
                                                            db->recovery.get());

    *db_out = db.release();
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_close(ceebase_db* db) {
    if (!db) {
        return CEEBASE_OK;
    }

    int rc = CEEBASE_OK;
    if (db->catalog) {
        auto cs = db->catalog->SaveCatalog();
        if (!cs.ok()) {
            rc = CEEBASE_ERROR;
        }
    }
    if (db->bpm) {
        auto fs = db->bpm->FlushAllPages();
        if (!fs.ok()) {
            rc = CEEBASE_ERROR;
        }
    }
    if (db->disk_mgr) {
        auto ss = db->disk_mgr->Sync();
        if (!ss.ok()) {
            rc = CEEBASE_ERROR;
        }
        db->disk_mgr->Close();
    }

    delete db;
    return rc;
}

CEEBASE_API const char* ceebase_errmsg(ceebase_db* db) {
    if (!db) return "Invalid database handle";
    return db->last_error.c_str();
}

CEEBASE_API int ceebase_exec(ceebase_db* db, const char* sql, char** errmsg) {
    if (!db || !sql) {
        return CEEBASE_ERROR;
    }

    auto res = db->engine->Execute(sql);
    db->last_changes = static_cast<int>(res.rows_affected);

    if (!res.success) {
        db->last_error = res.error_message;
        if (errmsg) {
            *errmsg = static_cast<char*>(std::malloc(res.error_message.size() + 1));
            if (*errmsg) {
                std::memcpy(*errmsg, res.error_message.c_str(), res.error_message.size() + 1);
            }
        }
        return CEEBASE_ERROR;
    }

    db->last_error.clear();
    return CEEBASE_OK;
}

CEEBASE_API void ceebase_free_errmsg(char* errmsg) {
    if (errmsg) {
        std::free(errmsg);
    }
}

CEEBASE_API int ceebase_prepare(ceebase_db* db, const char* sql, ceebase_stmt** stmt_out) {
    if (!db || !sql || !stmt_out) {
        return CEEBASE_ERROR;
    }

    *stmt_out = nullptr;
    auto stmt = std::make_unique<ceebase_stmt>();
    stmt->db = db;
    stmt->sql = sql;
    stmt->executed = false;
    stmt->current_row = 0;

    *stmt_out = stmt.release();
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_step(ceebase_stmt* stmt) {
    if (!stmt || !stmt->db) {
        return CEEBASE_ERROR;
    }

    if (!stmt->executed) {
        std::string sql_to_run = stmt->sql;
        if (!stmt->bound_params.empty()) {
            std::string substituted;
            int param_idx = 1;
            for (size_t i = 0; i < sql_to_run.size(); ++i) {
                if (sql_to_run[i] == '?') {
                    auto it = stmt->bound_params.find(param_idx++);
                    if (it != stmt->bound_params.end()) {
                        switch (it->second.type) {
                            case BoundParam::Type::INT:
                                substituted += std::to_string(it->second.int_val);
                                break;
                            case BoundParam::Type::INT64:
                                substituted += std::to_string(it->second.int64_val);
                                break;
                            case BoundParam::Type::DOUBLE: {
                                char buf[64];
                                std::snprintf(buf, sizeof(buf), "%.10f", it->second.double_val);
                                std::string s(buf);
                                while (s.size() > 2 && s.back() == '0' && s[s.size() - 2] != '.') {
                                    s.pop_back();
                                }
                                substituted += s;
                                break;
                            }
                            case BoundParam::Type::TEXT: {
                                substituted += '\'';
                                for (char c : it->second.text_val) {
                                    if (c == '\'') substituted += "''";
                                    else substituted += c;
                                }
                                substituted += '\'';
                                break;
                            }
                            case BoundParam::Type::NULL_VAL:
                                substituted += "NULL";
                                break;
                            default:
                                substituted += "NULL";
                                break;
                        }
                    } else {
                        substituted += "NULL";
                    }
                } else {
                    substituted += sql_to_run[i];
                }
            }
            sql_to_run = std::move(substituted);
        }

        stmt->result = stmt->db->engine->Execute(sql_to_run);
        stmt->executed = true;
        stmt->current_row = 0;
        stmt->db->last_changes = static_cast<int>(stmt->result.rows_affected);

        if (!stmt->result.success) {
            stmt->db->last_error = stmt->result.error_message;
            return CEEBASE_ERROR;
        }
    }

    if (stmt->current_row < stmt->result.rows.size()) {
        stmt->current_row++;
        return CEEBASE_ROW;
    }

    return CEEBASE_DONE;
}

CEEBASE_API int ceebase_reset(ceebase_stmt* stmt) {
    if (!stmt) return CEEBASE_ERROR;
    stmt->executed = false;
    stmt->current_row = 0;
    stmt->result = ceebase::QueryResult{};
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_finalize(ceebase_stmt* stmt) {
    delete stmt;
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_bind_int(ceebase_stmt* stmt, int param_idx, int32_t val) {
    if (!stmt || param_idx < 1) return CEEBASE_ERROR;
    stmt->bound_params[param_idx] = BoundParam{.type = BoundParam::Type::INT, .int_val = val};
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_bind_int64(ceebase_stmt* stmt, int param_idx, int64_t val) {
    if (!stmt || param_idx < 1) return CEEBASE_ERROR;
    stmt->bound_params[param_idx] = BoundParam{.type = BoundParam::Type::INT64, .int64_val = val};
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_bind_double(ceebase_stmt* stmt, int param_idx, double val) {
    if (!stmt || param_idx < 1) return CEEBASE_ERROR;
    stmt->bound_params[param_idx] = BoundParam{.type = BoundParam::Type::DOUBLE, .double_val = val};
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_bind_text(ceebase_stmt* stmt, int param_idx, const char* val, int len) {
    if (!stmt || param_idx < 1) return CEEBASE_ERROR;
    if (!val) {
        return ceebase_bind_null(stmt, param_idx);
    }
    std::string text_str = (len < 0) ? std::string(val) : std::string(val, static_cast<size_t>(len));
    stmt->bound_params[param_idx] = BoundParam{.type = BoundParam::Type::TEXT, .text_val = std::move(text_str)};
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_bind_null(ceebase_stmt* stmt, int param_idx) {
    if (!stmt || param_idx < 1) return CEEBASE_ERROR;
    stmt->bound_params[param_idx] = BoundParam{.type = BoundParam::Type::NULL_VAL};
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_clear_bindings(ceebase_stmt* stmt) {
    if (!stmt) return CEEBASE_ERROR;
    stmt->bound_params.clear();
    return CEEBASE_OK;
}

CEEBASE_API int ceebase_column_count(ceebase_stmt* stmt) {
    if (!stmt) return 0;
    return static_cast<int>(stmt->result.column_names.size());
}

CEEBASE_API const char* ceebase_column_name(ceebase_stmt* stmt, int col_idx) {
    if (!stmt || col_idx < 0 || static_cast<size_t>(col_idx) >= stmt->result.column_names.size()) {
        return nullptr;
    }
    return stmt->result.column_names[static_cast<size_t>(col_idx)].c_str();
}

CEEBASE_API int ceebase_column_type(ceebase_stmt* stmt, int col_idx) {
    if (!stmt || col_idx < 0 || static_cast<size_t>(col_idx) >= stmt->result.column_names.size()) {
        return CEEBASE_TYPE_NULL;
    }
    // Check if current row has NULL for this column
    if (stmt->current_row > 0) {
        size_t row_idx = stmt->current_row - 1;
        if (row_idx < stmt->result.null_indicators.size() &&
            static_cast<size_t>(col_idx) < stmt->result.null_indicators[row_idx].size() &&
            stmt->result.null_indicators[row_idx][static_cast<size_t>(col_idx)]) {
            return CEEBASE_TYPE_NULL;
        }
    }
    if (static_cast<size_t>(col_idx) < stmt->result.column_types.size()) {
        switch (stmt->result.column_types[static_cast<size_t>(col_idx)]) {
            case ceebase::DataType::INTEGER: return CEEBASE_TYPE_INTEGER;
            case ceebase::DataType::BIGINT:  return CEEBASE_TYPE_BIGINT;
            case ceebase::DataType::FLOAT:   return CEEBASE_TYPE_FLOAT;
            case ceebase::DataType::DOUBLE:  return CEEBASE_TYPE_DOUBLE;
            case ceebase::DataType::BOOLEAN: return CEEBASE_TYPE_BOOLEAN;
            case ceebase::DataType::TEXT:    return CEEBASE_TYPE_TEXT;
            default:                         return CEEBASE_TYPE_TEXT;
        }
    }
    return CEEBASE_TYPE_TEXT;
}

CEEBASE_API int ceebase_column_is_null(ceebase_stmt* stmt, int col_idx) {
    if (!stmt || stmt->current_row == 0 || col_idx < 0) return 1;
    size_t row_idx = stmt->current_row - 1;
    if (row_idx >= stmt->result.null_indicators.size()) return 0;
    if (static_cast<size_t>(col_idx) >= stmt->result.null_indicators[row_idx].size()) return 0;
    return stmt->result.null_indicators[row_idx][static_cast<size_t>(col_idx)] ? 1 : 0;
}

CEEBASE_API int32_t ceebase_column_int(ceebase_stmt* stmt, int col_idx) {
    const char* txt = ceebase_column_text(stmt, col_idx);
    if (!txt) return 0;
    try {
        return std::stoi(txt);
    } catch (...) {
        return 0;
    }
}

CEEBASE_API int64_t ceebase_column_int64(ceebase_stmt* stmt, int col_idx) {
    const char* txt = ceebase_column_text(stmt, col_idx);
    if (!txt) return 0;
    try {
        return std::stoll(txt);
    } catch (...) {
        return 0;
    }
}

CEEBASE_API double ceebase_column_double(ceebase_stmt* stmt, int col_idx) {
    const char* txt = ceebase_column_text(stmt, col_idx);
    if (!txt) return 0.0;
    try {
        return std::stod(txt);
    } catch (...) {
        return 0.0;
    }
}

CEEBASE_API int ceebase_column_boolean(ceebase_stmt* stmt, int col_idx) {
    const char* txt = ceebase_column_text(stmt, col_idx);
    if (!txt) return 0;
    if (std::strcmp(txt, "true") == 0 || std::strcmp(txt, "1") == 0 ||
        std::strcmp(txt, "TRUE") == 0 || std::strcmp(txt, "t") == 0) {
        return 1;
    }
    return 0;
}

CEEBASE_API const char* ceebase_column_text(ceebase_stmt* stmt, int col_idx) {
    if (!stmt || stmt->current_row == 0) return nullptr;
    size_t row_idx = stmt->current_row - 1;
    if (row_idx >= stmt->result.rows.size()) return nullptr;
    if (col_idx < 0 || static_cast<size_t>(col_idx) >= stmt->result.rows[row_idx].size()) {
        return nullptr;
    }
    return stmt->result.rows[row_idx][static_cast<size_t>(col_idx)].c_str();
}

CEEBASE_API int ceebase_changes(ceebase_db* db) {
    if (!db) return 0;
    return db->last_changes;
}

CEEBASE_API const char* ceebase_version(void) {
    return ceebase::CEEBASE_VERSION_STRING.data();
}

} // extern "C"
