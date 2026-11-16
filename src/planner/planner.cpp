#include "ceebase/planner/planner.hpp"

namespace ceebase {

Status Planner::ValidateStatement(const Statement* stmt) {
    if (!stmt) return Status::InvalidArgument("Null statement");

    switch (stmt->GetType()) {
        case StatementType::CREATE_TABLE: {
            auto* s = static_cast<const CreateTableStatement*>(stmt);
            if (s->GetTableName().empty()) return Status::InvalidArgument("Table name cannot be empty");
            if (s->GetColumns().empty()) return Status::InvalidArgument("Table must define at least one column");
            return Status::Ok();
        }
        case StatementType::DROP_TABLE: {
            auto* s = static_cast<const DropTableStatement*>(stmt);
            if (!catalog_->GetTable(s->GetTableName())) {
                return Status::NotFound("Table not found: " + s->GetTableName());
            }
            return Status::Ok();
        }
        case StatementType::CREATE_INDEX: {
            auto* s = static_cast<const CreateIndexStatement*>(stmt);
            auto* tbl = catalog_->GetTable(s->GetTableName());
            if (!tbl) return Status::NotFound("Table not found: " + s->GetTableName());
            if (tbl->schema.GetColumnIndex(s->GetColumnName()) == -1) {
                return Status::InvalidArgument("Column '" + s->GetColumnName() + "' not found in table '" + s->GetTableName() + "'");
            }
            return Status::Ok();
        }
        case StatementType::INSERT: {
            auto* s = static_cast<const InsertStatement*>(stmt);
            auto* tbl = catalog_->GetTable(s->GetTableName());
            if (!tbl) return Status::NotFound("Table not found: " + s->GetTableName());
            for (const auto& row : s->GetValuesList()) {
                if (s->GetColumns().empty()) {
                    if (row.size() != tbl->schema.GetColumnCount()) {
                        return Status::InvalidArgument("INSERT value count (" + std::to_string(row.size()) +
                                                       ") does not match table column count (" +
                                                       std::to_string(tbl->schema.GetColumnCount()) + ")");
                    }
                } else {
                    if (row.size() != s->GetColumns().size()) {
                        return Status::InvalidArgument("INSERT value count does not match specified column count");
                    }
                }
            }
            return Status::Ok();
        }
        case StatementType::SELECT: {
            auto* s = static_cast<const SelectStatement*>(stmt);
            auto* tbl = catalog_->GetTable(s->GetFromTable());
            if (!tbl) return Status::NotFound("Table not found in FROM: " + s->GetFromTable());

            if (!s->GetJoinTable().empty()) {
                auto* jtbl = catalog_->GetTable(s->GetJoinTable());
                if (!jtbl) return Status::NotFound("Table not found in JOIN: " + s->GetJoinTable());
            }

            if (s->GetWhereClause()) {
                auto st = ValidateExpression(s->GetWhereClause(), tbl->schema);
                if (!st.ok()) return st;
            }
            return Status::Ok();
        }
        case StatementType::UPDATE: {
            auto* s = static_cast<const UpdateStatement*>(stmt);
            auto* tbl = catalog_->GetTable(s->GetTableName());
            if (!tbl) return Status::NotFound("Table not found: " + s->GetTableName());
            for (const auto& [col, expr] : s->GetAssignments()) {
                if (tbl->schema.GetColumnIndex(col) == -1) {
                    return Status::InvalidArgument("Column '" + col + "' not in table '" + s->GetTableName() + "'");
                }
            }
            return Status::Ok();
        }
        case StatementType::DELETE_STMT: {
            auto* s = static_cast<const DeleteStatement*>(stmt);
            auto* tbl = catalog_->GetTable(s->GetTableName());
            if (!tbl) return Status::NotFound("Table not found: " + s->GetTableName());
            return Status::Ok();
        }
        case StatementType::EXPLAIN: {
            auto* s = static_cast<const ExplainStatement*>(stmt);
            return ValidateStatement(s->GetStatement());
        }
    }
    return Status::Ok();
}

Status Planner::ValidateExpression(const Expression* expr, const Schema& schema) {
    if (!expr) return Status::Ok();
    if (expr->GetType() == ExprType::COLUMN_REF) {
        auto* cref = static_cast<const ColumnRefExpr*>(expr);
        if (cref->GetColumnName() != "*" && schema.GetColumnIndex(cref->GetColumnName()) == -1) {
            return Status::InvalidArgument("Column '" + cref->GetColumnName() + "' not found in table schema");
        }
    } else if (expr->GetType() == ExprType::BINARY_OP) {
        auto* bop = static_cast<const BinaryOpExpr*>(expr);
        auto s1 = ValidateExpression(bop->GetLeft(), schema);
        if (!s1.ok()) return s1;
        return ValidateExpression(bop->GetRight(), schema);
    }
    return Status::Ok();
}

Result<std::unique_ptr<LogicalPlanNode>> Planner::PlanSelect(const SelectStatement* stmt) {
    auto val_status = ValidateStatement(stmt);
    if (!val_status.ok()) return val_status;

    auto* from_tbl = catalog_->GetTable(stmt->GetFromTable());
    std::unique_ptr<LogicalPlanNode> plan = std::make_unique<LogicalSeqScan>(stmt->GetFromTable(), from_tbl->schema);

    // 1. Join handling
    if (!stmt->GetJoinTable().empty()) {
        auto* join_tbl = catalog_->GetTable(stmt->GetJoinTable());
        std::unique_ptr<LogicalPlanNode> right = std::make_unique<LogicalSeqScan>(stmt->GetJoinTable(), join_tbl->schema);

        // Combined schema
        std::vector<Column> joined_cols;
        for (const auto& c : from_tbl->schema.GetColumns()) {
            joined_cols.push_back(Column(stmt->GetFromTable() + "." + c.GetName(), c.GetType(), c.IsNullable()));
        }
        for (const auto& c : join_tbl->schema.GetColumns()) {
            joined_cols.push_back(Column(stmt->GetJoinTable() + "." + c.GetName(), c.GetType(), c.IsNullable()));
        }
        Schema joined_schema(std::move(joined_cols));

        std::unique_ptr<Expression> join_pred = nullptr;
        if (stmt->GetJoinCondition()) {
            // Clone join condition
            // For simplicity in planning, keep reference or clone
        }
        plan = std::make_unique<LogicalJoin>(std::move(plan), std::move(right), nullptr, std::move(joined_schema));
    }

    // 2. WHERE filter handling
    if (stmt->GetWhereClause()) {
        plan = std::make_unique<LogicalFilter>(std::move(plan), stmt->GetWhereClause()->Clone());
    }

    // 3. GROUP BY and Aggregates
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
            int idx = from_tbl->schema.GetColumnIndex(g);
            if (idx != -1) {
                agg_cols.push_back(from_tbl->schema.GetColumn(static_cast<size_t>(idx)));
            }
        }
        for (const auto& [col_name, type] : aggs) {
            std::string name = std::string(AggregateTypeToString(type)) + "(" + col_name + ")";
            agg_cols.push_back(Column(name, (type == AggregateType::COUNT ? DataType::BIGINT : DataType::DOUBLE), true));
        }
        Schema agg_schema(std::move(agg_cols));
        plan = std::make_unique<LogicalAggregate>(std::move(plan), stmt->GetGroupBy(), std::move(aggs),
                                                  nullptr, std::move(agg_schema));
    }

    // 4. ORDER BY
    if (!stmt->GetOrderByColumn().empty()) {
        plan = std::make_unique<LogicalSort>(std::move(plan), stmt->GetOrderByColumn(), stmt->IsDesc());
    }

    // 5. LIMIT
    if (stmt->GetLimit() >= 0) {
        plan = std::make_unique<LogicalLimit>(std::move(plan), stmt->GetLimit());
    }

    return plan;
}

} // namespace ceebase
