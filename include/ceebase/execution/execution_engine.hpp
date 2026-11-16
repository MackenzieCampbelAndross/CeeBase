#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/concurrency/transaction_manager.hpp"
#include "ceebase/execution/executor.hpp"
#include "ceebase/parser/ast.hpp"
#include "ceebase/parser/lexer.hpp"
#include "ceebase/parser/parser.hpp"
#include "ceebase/planner/optimizer.hpp"
#include "ceebase/planner/planner.hpp"
#include "ceebase/recovery/recovery_engine.hpp"

namespace ceebase {

struct QueryResult {
    bool success{false};
    std::string error_message{};
    std::vector<std::string> column_names{};
    std::vector<std::vector<std::string>> rows{};
    size_t rows_affected{0};
    double execution_time_ms{0.0};
    std::string explain_output{};

    void Format(std::ostream& os) const;
};

class ExecutionEngine {
public:
    ExecutionEngine(BufferPoolManager* bpm, Catalog* catalog,
                    TransactionManager* txn_mgr = nullptr, RecoveryEngine* recovery = nullptr)
        : bpm_(bpm), catalog_(catalog), txn_mgr_(txn_mgr), recovery_(recovery),
          planner_(catalog), optimizer_(catalog) {}

    // Execute arbitrary SQL statement string
    QueryResult Execute(std::string_view sql);

private:
    QueryResult ExecuteStatement(const Statement* stmt);
    QueryResult ExecuteCreateTable(const CreateTableStatement* stmt);
    QueryResult ExecuteDropTable(const DropTableStatement* stmt);
    QueryResult ExecuteCreateIndex(const CreateIndexStatement* stmt);
    QueryResult ExecuteInsert(const InsertStatement* stmt);
    QueryResult ExecuteSelect(const SelectStatement* stmt);
    QueryResult ExecuteUpdate(const UpdateStatement* stmt);
    QueryResult ExecuteDelete(const DeleteStatement* stmt);
    QueryResult ExecuteExplain(const ExplainStatement* stmt);

    BufferPoolManager* bpm_;
    Catalog* catalog_;
    TransactionManager* txn_mgr_;
    RecoveryEngine* recovery_;
    Planner planner_;
    Optimizer optimizer_;
    std::unique_ptr<Transaction> active_txn_{nullptr};
};

} // namespace ceebase
