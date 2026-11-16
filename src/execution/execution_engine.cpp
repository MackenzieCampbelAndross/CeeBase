#include "nexus/execution/execution_engine.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "nexus/record/slotted_page.hpp"

namespace nexus {

void QueryResult::Format(std::ostream& os) const {
    if (!success) {
        os << "Error: " << error_message << "\n";
        return;
    }

    if (!explain_output.empty()) {
        os << explain_output << "\n";
        os << "Execution time: " << std::fixed << std::setprecision(3) << execution_time_ms << " ms\n";
        return;
    }

    if (column_names.empty()) {
        os << "Query OK, " << rows_affected << " row(s) affected ("
           << std::fixed << std::setprecision(3) << execution_time_ms << " ms)\n";
        return;
    }

    // Determine column widths
    std::vector<size_t> widths(column_names.size());
    for (size_t i = 0; i < column_names.size(); ++i) {
        widths[i] = column_names[i].size();
    }
    for (const auto& row : rows) {
        for (size_t i = 0; i < row.size(); ++i) {
            widths[i] = std::max(widths[i], row[i].size());
        }
    }

    // Print divider
    auto print_divider = [&]() {
        os << "+";
        for (size_t w : widths) {
            os << std::string(w + 2, '-') << "+";
        }
        os << "\n";
    };

    print_divider();
    // Header
    os << "|";
    for (size_t i = 0; i < column_names.size(); ++i) {
        os << " " << std::left << std::setw(static_cast<int>(widths[i])) << column_names[i] << " |";
    }
    os << "\n";
    print_divider();

    // Data rows
    for (const auto& row : rows) {
        os << "|";
        for (size_t i = 0; i < row.size(); ++i) {
            os << " " << std::left << std::setw(static_cast<int>(widths[i])) << row[i] << " |";
        }
        os << "\n";
    }
    print_divider();

    os << rows.size() << " row(s) in set (" << std::fixed << std::setprecision(3)
       << execution_time_ms << " ms)\n";
}

QueryResult ExecutionEngine::Execute(std::string_view sql) {
    auto start_time = std::chrono::high_resolution_clock::now();

    // Check transaction commands
    std::string trimmed(sql);
    while (!trimmed.empty() && (trimmed.back() == ' ' || trimmed.back() == ';' || trimmed.back() == '\n' || trimmed.back() == '\r')) {
        trimmed.pop_back();
    }
    std::string upper_sql = trimmed;
    for (char& c : upper_sql) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    if (upper_sql == "BEGIN" || upper_sql == "START TRANSACTION") {
        if (txn_mgr_) {
            active_txn_ = txn_mgr_->Begin();
            auto end_time = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            return QueryResult{true, "", {}, {}, 0, elapsed, ""};
        }
    } else if (upper_sql == "COMMIT") {
        if (txn_mgr_ && active_txn_) {
            auto s = txn_mgr_->Commit(active_txn_.get());
            active_txn_.reset();
            auto end_time = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            if (!s.ok()) return QueryResult{false, s.ToString(), {}, {}, 0, elapsed, ""};
            return QueryResult{true, "", {}, {}, 0, elapsed, ""};
        }
    } else if (upper_sql == "ROLLBACK") {
        if (txn_mgr_ && active_txn_) {
            auto s = txn_mgr_->Abort(active_txn_.get());
            active_txn_.reset();
            auto end_time = std::chrono::high_resolution_clock::now();
            double elapsed = std::chrono::duration<double, std::milli>(end_time - start_time).count();
            if (!s.ok()) return QueryResult{false, s.ToString(), {}, {}, 0, elapsed, ""};
            return QueryResult{true, "", {}, {}, 0, elapsed, ""};
        }
    }

    Lexer lexer(sql);
    auto tokens_res = lexer.Tokenize();
    if (!tokens_res.ok()) {
        return QueryResult{false, tokens_res.status().ToString(), {}, {}, 0, 0.0, ""};
    }

    Parser parser(std::move(*tokens_res));
    auto stmt_res = parser.ParseStatement();
    if (!stmt_res.ok()) {
        return QueryResult{false, stmt_res.status().ToString(), {}, {}, 0, 0.0, ""};
    }

    QueryResult result = ExecuteStatement(stmt_res->get());

    auto end_time = std::chrono::high_resolution_clock::now();
    result.execution_time_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();
    return result;
}

QueryResult ExecutionEngine::ExecuteStatement(const Statement* stmt) {
    if (!stmt) return QueryResult{false, "Null statement", {}, {}, 0, 0.0, ""};

    switch (stmt->GetType()) {
        case StatementType::CREATE_TABLE:
            return ExecuteCreateTable(static_cast<const CreateTableStatement*>(stmt));
        case StatementType::DROP_TABLE:
            return ExecuteDropTable(static_cast<const DropTableStatement*>(stmt));
        case StatementType::CREATE_INDEX:
            return ExecuteCreateIndex(static_cast<const CreateIndexStatement*>(stmt));
        case StatementType::INSERT:
            return ExecuteInsert(static_cast<const InsertStatement*>(stmt));
        case StatementType::SELECT:
            return ExecuteSelect(static_cast<const SelectStatement*>(stmt));
        case StatementType::UPDATE:
            return ExecuteUpdate(static_cast<const UpdateStatement*>(stmt));
        case StatementType::DELETE_STMT:
            return ExecuteDelete(static_cast<const DeleteStatement*>(stmt));
        case StatementType::EXPLAIN:
            return ExecuteExplain(static_cast<const ExplainStatement*>(stmt));
    }
    return QueryResult{false, "Unsupported statement type", {}, {}, 0, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteCreateTable(const CreateTableStatement* stmt) {
    std::vector<Column> cols;
    std::string pk_col = "";
    for (const auto& cdef : stmt->GetColumns()) {
        cols.emplace_back(cdef.name, cdef.type, cdef.is_nullable);
        if (cdef.is_primary_key) {
            pk_col = cdef.name;
        }
    }
    Schema schema(std::move(cols));

    auto res = catalog_->CreateTable(stmt->GetTableName(), std::move(schema), pk_col);
    if (!res.ok()) {
        return QueryResult{false, res.status().ToString(), {}, {}, 0, 0.0, ""};
    }
    return QueryResult{true, "", {}, {}, 0, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteDropTable(const DropTableStatement* stmt) {
    auto s = catalog_->DropTable(stmt->GetTableName());
    if (!s.ok()) {
        return QueryResult{false, s.ToString(), {}, {}, 0, 0.0, ""};
    }
    return QueryResult{true, "", {}, {}, 0, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteCreateIndex(const CreateIndexStatement* stmt) {
    auto res = catalog_->CreateIndex(stmt->GetIndexName(), stmt->GetTableName(), stmt->GetColumnName());
    if (!res.ok()) {
        return QueryResult{false, res.status().ToString(), {}, {}, 0, 0.0, ""};
    }
    return QueryResult{true, "", {}, {}, 0, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteInsert(const InsertStatement* stmt) {
    auto* tbl = catalog_->GetTable(stmt->GetTableName());
    if (!tbl) {
        return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0, ""};
    }

    bool autocommit = (active_txn_ == nullptr && txn_mgr_ != nullptr);
    if (autocommit) {
        active_txn_ = txn_mgr_->Begin();
    }

    size_t rows_inserted = 0;
    auto indexes = catalog_->GetTableIndexes(stmt->GetTableName());

    for (const auto& row_exprs : stmt->GetValuesList()) {
        std::vector<Value> vals;
        vals.reserve(row_exprs.size());
        for (const auto& expr : row_exprs) {
            vals.push_back(ExpressionEvaluator::Evaluate(expr.get(), nullptr, nullptr));
        }

        Tuple tuple(vals, tbl->schema);

        // Check primary key constraint if primary key column is configured
        if (!tbl->primary_key_column.empty()) {
            int pk_idx = tbl->schema.GetColumnIndex(tbl->primary_key_column);
            if (pk_idx != -1) {
                Value pk_val = tuple.GetValue(tbl->schema, static_cast<size_t>(pk_idx));
                int64_t pk_k = (pk_val.GetType() == DataType::INTEGER) ? pk_val.GetInteger() : pk_val.GetBigInt();
                auto* pk_idx_meta = catalog_->GetIndex(tbl->name + "_pk_idx");
                if (pk_idx_meta) {
                    RID dummy_rid;
                    if (pk_idx_meta->index->GetValue(pk_k, &dummy_rid)) {
                        if (autocommit) {
                            (void)txn_mgr_->Abort(active_txn_.get());
                            active_txn_.reset();
                        }
                        return QueryResult{false, "Duplicate primary key value: " + std::to_string(pk_k), {}, {}, rows_inserted, 0.0, ""};
                    }
                }
            }
        }

        RID rid;
        auto s = tbl->table_heap->InsertTuple(tuple, &rid);
        if (!s.ok()) {
            if (autocommit) {
                (void)txn_mgr_->Abort(active_txn_.get());
                active_txn_.reset();
            }
            return QueryResult{false, s.ToString(), {}, {}, rows_inserted, 0.0, ""};
        }

        // Update indexes
        for (auto* idx_meta : indexes) {
            int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
            if (col_idx != -1) {
                Value key_val = tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                int64_t k = 0;
                if (key_val.GetType() == DataType::INTEGER) k = key_val.GetInteger();
                else if (key_val.GetType() == DataType::BIGINT) k = key_val.GetBigInt();
                (void)idx_meta->index->Insert(k, rid);
            }
        }

        // Record in active transaction and log
        if (active_txn_) {
            if (txn_mgr_ && txn_mgr_->GetLogManager()) {
                LogRecord rec = LogRecord::MakeInsert(
                    active_txn_->GetTxnId(),
                    active_txn_->GetPrevLSN(),
                    tbl->name,
                    rid,
                    tuple.GetDataVector()
                );
                lsn_t lsn = txn_mgr_->GetLogManager()->AppendRecord(&rec);
                active_txn_->SetPrevLSN(lsn);

                auto page_res = bpm_->FetchPage(rid.page_id);
                if (page_res.ok()) {
                    Page* p = *page_res;
                    p->SetLSN(lsn);
                    SlottedPage(p->GetData()).SetLSN(lsn);
                    (void)bpm_->UnpinPage(rid.page_id, true);
                }
            }
            active_txn_->RecordInsert(tbl, rid);
        }

        rows_inserted++;
    }

    if (autocommit) {
        Status s = txn_mgr_->Commit(active_txn_.get());
        active_txn_.reset();
        if (!s.ok()) {
            return QueryResult{false, s.ToString(), {}, {}, rows_inserted, 0.0, ""};
        }
    }

    return QueryResult{true, "", {}, {}, rows_inserted, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteSelect(const SelectStatement* stmt) {
    auto plan_res = planner_.PlanSelect(stmt);
    if (!plan_res.ok()) {
        return QueryResult{false, plan_res.status().ToString(), {}, {}, 0, 0.0, ""};
    }

    // Optimize plan
    auto opt_plan = optimizer_.Optimize(std::move(*plan_res));

    ExecutionContext exec_ctx(bpm_, catalog_);
    std::unique_ptr<AbstractExecutor> root_executor = nullptr;

    auto* tbl = catalog_->GetTable(stmt->GetFromTable());
    if (!tbl) {
        return QueryResult{false, "Table not found: " + stmt->GetFromTable(), {}, {}, 0, 0.0, ""};
    }

    // Check if optimizer selected IndexScan!
    if (opt_plan->GetType() == PlanType::INDEX_SCAN) {
        auto* iscan = static_cast<LogicalIndexScan*>(opt_plan.get());
        auto* idx_meta = catalog_->GetIndex(iscan->GetIndexName());
        root_executor = std::make_unique<IndexScanExecutor>(&exec_ctx, tbl, idx_meta, iscan->GetKeyValue());
    } else {
        root_executor = std::make_unique<SeqScanExecutor>(&exec_ctx, tbl);

        // Filter
        if (stmt->GetWhereClause()) {
            root_executor = std::make_unique<FilterExecutor>(&exec_ctx, std::move(root_executor), stmt->GetWhereClause());
        }
    }

    // Join
    if (!stmt->GetJoinTable().empty()) {
        auto* jtbl = catalog_->GetTable(stmt->GetJoinTable());
        if (jtbl) {
            auto right_exec = std::make_unique<SeqScanExecutor>(&exec_ctx, jtbl);
            std::vector<Column> joined_cols;
            for (const auto& c : tbl->schema.GetColumns()) {
                joined_cols.emplace_back(tbl->name + "." + c.GetName(), c.GetType(), c.IsNullable());
            }
            for (const auto& c : jtbl->schema.GetColumns()) {
                joined_cols.emplace_back(jtbl->name + "." + c.GetName(), c.GetType(), c.IsNullable());
            }
            Schema joined_schema(std::move(joined_cols));
            root_executor = std::make_unique<NestedLoopJoinExecutor>(&exec_ctx, std::move(root_executor),
                                                                    std::move(right_exec), stmt->GetJoinCondition(),
                                                                    std::move(joined_schema));
        }
    }

    // Group By & Aggregation
    bool has_agg = false;
    std::vector<std::pair<std::string, AggregateType>> aggs;
    for (const auto& e : stmt->GetSelectList()) {
        if (e->GetType() == ExprType::AGGREGATE) {
            has_agg = true;
            auto* agg = static_cast<const AggregateExpr*>(e.get());
            std::string col = agg->IsStar() ? "*" : (agg->GetArg() ? agg->GetArg()->ToString() : "");
            aggs.emplace_back(col, agg->GetAggregateType());
        }
    }

    if (!stmt->GetGroupBy().empty() || has_agg) {
        std::vector<Column> agg_cols;
        for (const auto& g : stmt->GetGroupBy()) {
            int idx = tbl->schema.GetColumnIndex(g);
            if (idx != -1) agg_cols.push_back(tbl->schema.GetColumn(static_cast<size_t>(idx)));
        }
        for (const auto& [col_name, type] : aggs) {
            std::string name = std::string(AggregateTypeToString(type)) + "(" + col_name + ")";
            agg_cols.emplace_back(name, (type == AggregateType::COUNT ? DataType::BIGINT : DataType::DOUBLE), true);
        }
        Schema agg_schema(std::move(agg_cols));
        root_executor = std::make_unique<AggregationExecutor>(&exec_ctx, std::move(root_executor),
                                                             stmt->GetGroupBy(), std::move(aggs),
                                                             stmt->GetHavingClause(), std::move(agg_schema));
    }

    // Sort
    if (!stmt->GetOrderByColumn().empty()) {
        root_executor = std::make_unique<SortExecutor>(&exec_ctx, std::move(root_executor),
                                                      stmt->GetOrderByColumn(), stmt->IsDesc());
    }

    // Limit
    if (stmt->GetLimit() >= 0) {
        root_executor = std::make_unique<LimitExecutor>(&exec_ctx, std::move(root_executor),
                                                       static_cast<size_t>(stmt->GetLimit()));
    }

    root_executor->Init();

    // Prepare Output Schema & Columns
    std::vector<std::string> output_headers;
    bool is_select_star = (stmt->GetSelectList().size() == 1 &&
                           stmt->GetSelectList()[0]->GetType() == ExprType::COLUMN_REF &&
                           static_cast<const ColumnRefExpr*>(stmt->GetSelectList()[0].get())->GetColumnName() == "*");

    const auto& cur_schema = root_executor->GetOutputSchema();
    if (is_select_star) {
        for (const auto& col : cur_schema.GetColumns()) {
            output_headers.push_back(col.GetName());
        }
    } else {
        for (const auto& e : stmt->GetSelectList()) {
            output_headers.push_back(e->ToString());
        }
    }

    // Materialize Rows
    std::vector<std::vector<std::string>> result_rows;
    Tuple row_tuple;
    RID row_rid;

    while (root_executor->Next(&row_tuple, &row_rid)) {
        std::vector<std::string> row_strings;
        if (is_select_star) {
            for (size_t i = 0; i < cur_schema.GetColumnCount(); ++i) {
                row_strings.push_back(row_tuple.GetValue(cur_schema, i).ToString());
            }
        } else {
            for (const auto& e : stmt->GetSelectList()) {
                Value v = ExpressionEvaluator::Evaluate(e.get(), &row_tuple, &cur_schema);
                row_strings.push_back(v.ToString());
            }
        }
        result_rows.push_back(std::move(row_strings));
    }

    return QueryResult{true, "", std::move(output_headers), std::move(result_rows), result_rows.size(), 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteUpdate(const UpdateStatement* stmt) {
    auto* tbl = catalog_->GetTable(stmt->GetTableName());
    if (!tbl) return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0, ""};

    bool autocommit = (active_txn_ == nullptr && txn_mgr_ != nullptr);
    if (autocommit) {
        active_txn_ = txn_mgr_->Begin();
    }

    size_t rows_updated = 0;
    auto indexes = catalog_->GetTableIndexes(stmt->GetTableName());
    std::vector<Tuple> target_tuples;
    bool used_index = false;

    // Check if WHERE clause can be served by an index point lookup: col = <constant>
    if (stmt->GetWhereClause() && stmt->GetWhereClause()->GetType() == ExprType::BINARY_OP) {
        auto* bop = static_cast<const BinaryOpExpr*>(stmt->GetWhereClause());
        if (bop->GetOp() == BinaryOp::EQUALS) {
            const ColumnRefExpr* col_ref = nullptr;
            const LiteralExpr* lit_ref = nullptr;
            if (bop->GetLeft()->GetType() == ExprType::COLUMN_REF && bop->GetRight()->GetType() == ExprType::LITERAL) {
                col_ref = static_cast<const ColumnRefExpr*>(bop->GetLeft());
                lit_ref = static_cast<const LiteralExpr*>(bop->GetRight());
            } else if (bop->GetRight()->GetType() == ExprType::COLUMN_REF && bop->GetLeft()->GetType() == ExprType::LITERAL) {
                col_ref = static_cast<const ColumnRefExpr*>(bop->GetRight());
                lit_ref = static_cast<const LiteralExpr*>(bop->GetLeft());
            }
            if (col_ref && lit_ref) {
                auto* idx_meta = catalog_->GetIndexForColumn(stmt->GetTableName(), col_ref->GetColumnName());
                if (idx_meta != nullptr) {
                    int64_t key_val = 0;
                    const Value& v = lit_ref->GetValue();
                    if (v.GetType() == DataType::INTEGER) key_val = v.GetInteger();
                    else if (v.GetType() == DataType::BIGINT) key_val = v.GetBigInt();
                    RID match_rid;
                    if (idx_meta->index->GetValue(key_val, &match_rid)) {
                        auto tup_res = tbl->table_heap->GetTuple(match_rid);
                        if (tup_res.ok()) {
                            target_tuples.push_back(std::move(*tup_res));
                        }
                    }
                    used_index = true;
                }
            }
        }
    }

    if (!used_index) {
        for (auto it = tbl->table_heap->Begin(); it != tbl->table_heap->End(); ++it) {
            Tuple cur_tuple = *it;
            if (stmt->GetWhereClause()) {
                Value cond = ExpressionEvaluator::Evaluate(stmt->GetWhereClause(), &cur_tuple, &tbl->schema);
                if (cond.IsNull() || !cond.GetBoolean()) {
                    continue;
                }
            }
            target_tuples.push_back(std::move(cur_tuple));
        }
    }

    for (const auto& cur_tuple : target_tuples) {

        // Build updated tuple
        std::vector<Value> new_vals;
        for (size_t i = 0; i < tbl->schema.GetColumnCount(); ++i) {
            const auto& col = tbl->schema.GetColumn(i);
            bool updated = false;
            for (const auto& [col_name, expr] : stmt->GetAssignments()) {
                if (col_name == col.GetName()) {
                    new_vals.push_back(ExpressionEvaluator::Evaluate(expr.get(), &cur_tuple, &tbl->schema));
                    updated = true;
                    break;
                }
            }
            if (!updated) {
                new_vals.push_back(cur_tuple.GetValue(tbl->schema, i));
            }
        }

        Tuple updated_tuple(new_vals, tbl->schema);

        // Check primary key collision if changed
        if (!tbl->primary_key_column.empty()) {
            int pk_idx = tbl->schema.GetColumnIndex(tbl->primary_key_column);
            if (pk_idx != -1) {
                Value old_pk_val = cur_tuple.GetValue(tbl->schema, static_cast<size_t>(pk_idx));
                Value new_pk_val = updated_tuple.GetValue(tbl->schema, static_cast<size_t>(pk_idx));
                int64_t old_pk = (old_pk_val.GetType() == DataType::INTEGER) ? old_pk_val.GetInteger() : old_pk_val.GetBigInt();
                int64_t new_pk = (new_pk_val.GetType() == DataType::INTEGER) ? new_pk_val.GetInteger() : new_pk_val.GetBigInt();
                if (old_pk != new_pk) {
                    auto* pk_idx_meta = catalog_->GetIndex(tbl->name + "_pk_idx");
                    if (pk_idx_meta) {
                        RID dummy_rid;
                        if (pk_idx_meta->index->GetValue(new_pk, &dummy_rid)) {
                            if (autocommit) {
                                (void)txn_mgr_->Abort(active_txn_.get());
                                active_txn_.reset();
                            }
                            return QueryResult{false, "Duplicate primary key value: " + std::to_string(new_pk), {}, {}, rows_updated, 0.0, ""};
                        }
                    }
                }
            }
        }

        // Update tuple in table heap
        auto s = tbl->table_heap->UpdateTuple(updated_tuple, cur_tuple.GetRID());
        if (!s.ok()) {
            if (autocommit) {
                (void)txn_mgr_->Abort(active_txn_.get());
                active_txn_.reset();
            }
            return QueryResult{false, s.ToString(), {}, {}, rows_updated, 0.0, ""};
        }

        // Update indexes if index keys changed
        for (auto* idx_meta : indexes) {
            int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
            if (col_idx != -1) {
                Value old_val = cur_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                Value new_val = updated_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                int64_t old_k = (old_val.GetType() == DataType::INTEGER) ? old_val.GetInteger() : old_val.GetBigInt();
                int64_t new_k = (new_val.GetType() == DataType::INTEGER) ? new_val.GetInteger() : new_val.GetBigInt();
                if (old_k != new_k) {
                    (void)idx_meta->index->Remove(old_k);
                    (void)idx_meta->index->Insert(new_k, cur_tuple.GetRID());
                }
            }
        }

        // Record in active transaction and write WAL
        if (active_txn_) {
            if (txn_mgr_ && txn_mgr_->GetLogManager()) {
                LogRecord rec = LogRecord::MakeUpdate(
                    active_txn_->GetTxnId(),
                    active_txn_->GetPrevLSN(),
                    tbl->name,
                    cur_tuple.GetRID(),
                    cur_tuple.GetDataVector(),
                    updated_tuple.GetDataVector()
                );
                lsn_t lsn = txn_mgr_->GetLogManager()->AppendRecord(&rec);
                active_txn_->SetPrevLSN(lsn);

                auto page_res = bpm_->FetchPage(cur_tuple.GetRID().page_id);
                if (page_res.ok()) {
                    Page* p = *page_res;
                    p->SetLSN(lsn);
                    SlottedPage(p->GetData()).SetLSN(lsn);
                    (void)bpm_->UnpinPage(cur_tuple.GetRID().page_id, true);
                }
            }
            active_txn_->RecordUpdate(tbl, cur_tuple.GetRID(), cur_tuple);
        }

        rows_updated++;
    }

    if (autocommit) {
        Status s = txn_mgr_->Commit(active_txn_.get());
        active_txn_.reset();
        if (!s.ok()) {
            return QueryResult{false, s.ToString(), {}, {}, rows_updated, 0.0, ""};
        }
    }

    return QueryResult{true, "", {}, {}, rows_updated, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteDelete(const DeleteStatement* stmt) {
    auto* tbl = catalog_->GetTable(stmt->GetTableName());
    if (!tbl) return QueryResult{false, "Table not found: " + stmt->GetTableName(), {}, {}, 0, 0.0, ""};

    bool autocommit = (active_txn_ == nullptr && txn_mgr_ != nullptr);
    if (autocommit) {
        active_txn_ = txn_mgr_->Begin();
    }

    size_t rows_deleted = 0;
    auto indexes = catalog_->GetTableIndexes(stmt->GetTableName());
    std::vector<Tuple> to_delete;
    bool used_index = false;

    // Check if WHERE clause can be served by an index point lookup: col = <constant>
    if (stmt->GetWhereClause() && stmt->GetWhereClause()->GetType() == ExprType::BINARY_OP) {
        auto* bop = static_cast<const BinaryOpExpr*>(stmt->GetWhereClause());
        if (bop->GetOp() == BinaryOp::EQUALS) {
            const ColumnRefExpr* col_ref = nullptr;
            const LiteralExpr* lit_ref = nullptr;
            if (bop->GetLeft()->GetType() == ExprType::COLUMN_REF && bop->GetRight()->GetType() == ExprType::LITERAL) {
                col_ref = static_cast<const ColumnRefExpr*>(bop->GetLeft());
                lit_ref = static_cast<const LiteralExpr*>(bop->GetRight());
            } else if (bop->GetRight()->GetType() == ExprType::COLUMN_REF && bop->GetLeft()->GetType() == ExprType::LITERAL) {
                col_ref = static_cast<const ColumnRefExpr*>(bop->GetRight());
                lit_ref = static_cast<const LiteralExpr*>(bop->GetLeft());
            }
            if (col_ref && lit_ref) {
                auto* idx_meta = catalog_->GetIndexForColumn(stmt->GetTableName(), col_ref->GetColumnName());
                if (idx_meta != nullptr) {
                    int64_t key_val = 0;
                    const Value& v = lit_ref->GetValue();
                    if (v.GetType() == DataType::INTEGER) key_val = v.GetInteger();
                    else if (v.GetType() == DataType::BIGINT) key_val = v.GetBigInt();
                    RID match_rid;
                    if (idx_meta->index->GetValue(key_val, &match_rid)) {
                        auto tup_res = tbl->table_heap->GetTuple(match_rid);
                        if (tup_res.ok()) {
                            to_delete.push_back(std::move(*tup_res));
                        }
                    }
                    used_index = true;
                }
            }
        }
    }

    if (!used_index) {
        for (auto it = tbl->table_heap->Begin(); it != tbl->table_heap->End(); ++it) {
            Tuple cur_tuple = *it;
            if (stmt->GetWhereClause()) {
                Value cond = ExpressionEvaluator::Evaluate(stmt->GetWhereClause(), &cur_tuple, &tbl->schema);
                if (cond.IsNull() || !cond.GetBoolean()) {
                    continue;
                }
            }
            to_delete.push_back(std::move(cur_tuple));
        }
    }

    for (const auto& cur_tuple : to_delete) {
        RID rid = cur_tuple.GetRID();
        auto s = tbl->table_heap->MarkDelete(rid);
        if (!s.ok()) {
            if (autocommit) {
                (void)txn_mgr_->Abort(active_txn_.get());
                active_txn_.reset();
            }
            return QueryResult{false, s.ToString(), {}, {}, rows_deleted, 0.0, ""};
        }

        // Remove from indexes
        for (auto* idx_meta : indexes) {
            int col_idx = tbl->schema.GetColumnIndex(idx_meta->key_column_name);
            if (col_idx != -1) {
                Value key_val = cur_tuple.GetValue(tbl->schema, static_cast<size_t>(col_idx));
                int64_t k = (key_val.GetType() == DataType::INTEGER) ? key_val.GetInteger() : key_val.GetBigInt();
                (void)idx_meta->index->Remove(k);
            }
        }

        // Record in active transaction and write WAL
        if (active_txn_) {
            if (txn_mgr_ && txn_mgr_->GetLogManager()) {
                LogRecord rec = LogRecord::MakeDelete(
                    active_txn_->GetTxnId(),
                    active_txn_->GetPrevLSN(),
                    tbl->name,
                    rid,
                    cur_tuple.GetDataVector()
                );
                lsn_t lsn = txn_mgr_->GetLogManager()->AppendRecord(&rec);
                active_txn_->SetPrevLSN(lsn);

                auto page_res = bpm_->FetchPage(rid.page_id);
                if (page_res.ok()) {
                    Page* p = *page_res;
                    p->SetLSN(lsn);
                    SlottedPage(p->GetData()).SetLSN(lsn);
                    (void)bpm_->UnpinPage(rid.page_id, true);
                }
            }
            active_txn_->RecordDelete(tbl, rid, cur_tuple);
        }

        rows_deleted++;
    }

    if (autocommit) {
        Status s = txn_mgr_->Commit(active_txn_.get());
        active_txn_.reset();
        if (!s.ok()) {
            return QueryResult{false, s.ToString(), {}, {}, rows_deleted, 0.0, ""};
        }
    }

    return QueryResult{true, "", {}, {}, rows_deleted, 0.0, ""};
}

QueryResult ExecutionEngine::ExecuteExplain(const ExplainStatement* stmt) {
    if (!stmt || !stmt->GetStatement()) {
        return QueryResult{false, "Empty EXPLAIN statement", {}, {}, 0, 0.0, ""};
    }

    std::ostringstream ss;
    ss << "=== NexusDB Query Execution Plan ===\n\n";

    if (stmt->GetStatement()->GetType() == StatementType::SELECT) {
        auto* sel = static_cast<const SelectStatement*>(stmt->GetStatement());
        auto plan_res = planner_.PlanSelect(sel);
        if (!plan_res.ok()) {
            return QueryResult{false, plan_res.status().ToString(), {}, {}, 0, 0.0, ""};
        }

        ss << "[1. Logical Query Plan]:\n";
        ss << (*plan_res)->ToString(1);

        auto opt_plan = optimizer_.Optimize(std::move(*plan_res));
        PlanCost cost = optimizer_.EstimateCost(opt_plan.get());

        ss << "\n[2. Physical Execution Plan]:\n";
        ss << opt_plan->ToString(1);

        ss << "\n[3. Optimizer Diagnostics]:\n";
        ss << "  Selected Access Path: " << cost.access_path << "\n";
        ss << "  Index Used:           " << (cost.used_index ? "YES" : "NO") << "\n";
        ss << "  Estimated I/O Cost:   " << cost.estimated_cost << "\n";
        ss << "  Est. Cardinality:     " << cost.estimated_cardinality << " row(s)\n";
    } else {
        ss << "EXPLAIN is currently displaying plans for SELECT queries.\n";
    }

    return QueryResult{true, "", {}, {}, 0, 0.0, ss.str()};
}

} // namespace nexus
