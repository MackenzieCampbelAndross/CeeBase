#pragma once

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "ceebase/ceebase_c.h"

namespace ceebase {

class Exception : public std::runtime_error {
public:
    explicit Exception(const std::string& msg) : std::runtime_error(msg) {}
};

class Statement {
public:
    explicit Statement(ceebase_stmt* stmt) : stmt_(stmt) {}
    ~Statement() {
        if (stmt_) {
            ceebase_finalize(stmt_);
            stmt_ = nullptr;
        }
    }

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;

    Statement(Statement&& other) noexcept : stmt_(other.stmt_) {
        other.stmt_ = nullptr;
    }
    Statement& operator=(Statement&& other) noexcept {
        if (this != &other) {
            if (stmt_) ceebase_finalize(stmt_);
            stmt_ = other.stmt_;
            other.stmt_ = nullptr;
        }
        return *this;
    }

    void Bind(int idx, int32_t val) {
        if (ceebase_bind_int(stmt_, idx, val) != CEEBASE_OK) {
            throw Exception("Failed to bind int parameter at index " + std::to_string(idx));
        }
    }

    void Bind(int idx, int64_t val) {
        if (ceebase_bind_int64(stmt_, idx, val) != CEEBASE_OK) {
            throw Exception("Failed to bind int64 parameter at index " + std::to_string(idx));
        }
    }

    void Bind(int idx, double val) {
        if (ceebase_bind_double(stmt_, idx, val) != CEEBASE_OK) {
            throw Exception("Failed to bind double parameter at index " + std::to_string(idx));
        }
    }

    void Bind(int idx, std::string_view val) {
        if (ceebase_bind_text(stmt_, idx, val.data(), static_cast<int>(val.size())) != CEEBASE_OK) {
            throw Exception("Failed to bind text parameter at index " + std::to_string(idx));
        }
    }

    void BindNull(int idx) {
        if (ceebase_bind_null(stmt_, idx) != CEEBASE_OK) {
            throw Exception("Failed to bind null parameter at index " + std::to_string(idx));
        }
    }

    bool Step() {
        int rc = ceebase_step(stmt_);
        if (rc == CEEBASE_ROW) return true;
        if (rc == CEEBASE_DONE) return false;
        throw Exception("Statement execution error during step");
    }

    void Reset() {
        if (ceebase_reset(stmt_) != CEEBASE_OK) {
            throw Exception("Failed to reset statement");
        }
    }

    [[nodiscard]] int ColumnCount() const noexcept {
        return ceebase_column_count(stmt_);
    }

    [[nodiscard]] std::string ColumnName(int idx) const {
        const char* name = ceebase_column_name(stmt_, idx);
        return name ? std::string(name) : "";
    }

    [[nodiscard]] bool IsNull(int idx) const noexcept {
        return ceebase_column_is_null(stmt_, idx) != 0;
    }

    [[nodiscard]] int32_t GetInt(int idx) const noexcept {
        return ceebase_column_int(stmt_, idx);
    }

    [[nodiscard]] int64_t GetInt64(int idx) const noexcept {
        return ceebase_column_int64(stmt_, idx);
    }

    [[nodiscard]] double GetDouble(int idx) const noexcept {
        return ceebase_column_double(stmt_, idx);
    }

    [[nodiscard]] std::string GetString(int idx) const {
        const char* txt = ceebase_column_text(stmt_, idx);
        return txt ? std::string(txt) : "";
    }

private:
    ceebase_stmt* stmt_{nullptr};
};

class Connection {
public:
    explicit Connection(const std::string& path) {
        ceebase_db* db = nullptr;
        if (ceebase_open(path.c_str(), &db) != CEEBASE_OK || !db) {
            std::string err = db ? ceebase_errmsg(db) : "Failed to allocate database handle";
            if (db) ceebase_close(db);
            throw Exception("Failed to open CeeBase database: " + err);
        }
        db_ = db;
    }

    ~Connection() {
        Close();
    }

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    Connection(Connection&& other) noexcept : db_(other.db_) {
        other.db_ = nullptr;
    }
    Connection& operator=(Connection&& other) noexcept {
        if (this != &other) {
            Close();
            db_ = other.db_;
            other.db_ = nullptr;
        }
        return *this;
    }

    void Close() noexcept {
        if (db_) {
            ceebase_close(db_);
            db_ = nullptr;
        }
    }

    void Execute(std::string_view sql) {
        char* errmsg = nullptr;
        int rc = ceebase_exec(db_, sql.data(), &errmsg);
        if (rc != CEEBASE_OK) {
            std::string err = errmsg ? errmsg : ceebase_errmsg(db_);
            ceebase_free_errmsg(errmsg);
            throw Exception("SQL execution error: " + err);
        }
    }

    Statement Prepare(std::string_view sql) {
        ceebase_stmt* stmt = nullptr;
        if (ceebase_prepare(db_, sql.data(), &stmt) != CEEBASE_OK || !stmt) {
            throw Exception("Failed to prepare statement: " + std::string(ceebase_errmsg(db_)));
        }
        return Statement(stmt);
    }

    void Begin() { Execute("BEGIN;"); }
    void Commit() { Execute("COMMIT;"); }
    void Rollback() { Execute("ROLLBACK;"); }

    [[nodiscard]] int Changes() const noexcept {
        return ceebase_changes(db_);
    }

    [[nodiscard]] static std::string Version() noexcept {
        return std::string(ceebase_version());
    }

private:
    ceebase_db* db_{nullptr};
};

} // namespace ceebase
