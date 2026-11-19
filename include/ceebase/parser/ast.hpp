#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ceebase/common/types.hpp"
#include "ceebase/record/value.hpp"

namespace ceebase {

enum class ExprType {
    LITERAL,
    COLUMN_REF,
    BINARY_OP,
    AGGREGATE
};

enum class BinaryOp {
    EQUALS,
    NOT_EQUALS,
    LESS,
    LESS_EQUALS,
    GREATER,
    GREATER_EQUALS,
    AND,
    OR,
    ADD,
    SUBTRACT,
    MULTIPLY,
    DIVIDE
};

[[nodiscard]] std::string_view BinaryOpToString(BinaryOp op) noexcept;

enum class AggregateType {
    COUNT,
    SUM,
    AVG,
    MIN,
    MAX
};

[[nodiscard]] std::string_view AggregateTypeToString(AggregateType type) noexcept;

class Expression {
public:
    virtual ~Expression() = default;
    [[nodiscard]] virtual ExprType GetType() const noexcept = 0;
    [[nodiscard]] virtual std::string ToString() const = 0;
    [[nodiscard]] virtual std::unique_ptr<Expression> Clone() const = 0;
};

class LiteralExpr : public Expression {
public:
    explicit LiteralExpr(Value val) : val_(std::move(val)) {}
    [[nodiscard]] ExprType GetType() const noexcept override { return ExprType::LITERAL; }
    [[nodiscard]] const Value& GetValue() const noexcept { return val_; }
    [[nodiscard]] std::string ToString() const override { return val_.ToString(); }
    [[nodiscard]] std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<LiteralExpr>(val_);
    }

private:
    Value val_;
};

class ColumnRefExpr : public Expression {
public:
    explicit ColumnRefExpr(std::string col_name, std::string tbl_name = "")
        : col_name_(std::move(col_name)), tbl_name_(std::move(tbl_name)) {}

    [[nodiscard]] ExprType GetType() const noexcept override { return ExprType::COLUMN_REF; }
    [[nodiscard]] const std::string& GetColumnName() const noexcept { return col_name_; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return tbl_name_; }

    [[nodiscard]] std::string ToString() const override {
        return tbl_name_.empty() ? col_name_ : tbl_name_ + "." + col_name_;
    }

    [[nodiscard]] std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<ColumnRefExpr>(col_name_, tbl_name_);
    }

private:
    std::string col_name_;
    std::string tbl_name_;
};

class BinaryOpExpr : public Expression {
public:
    BinaryOpExpr(std::unique_ptr<Expression> left, BinaryOp op, std::unique_ptr<Expression> right)
        : left_(std::move(left)), op_(op), right_(std::move(right)) {}

    [[nodiscard]] ExprType GetType() const noexcept override { return ExprType::BINARY_OP; }
    [[nodiscard]] const Expression* GetLeft() const noexcept { return left_.get(); }
    [[nodiscard]] const Expression* GetRight() const noexcept { return right_.get(); }
    [[nodiscard]] BinaryOp GetOp() const noexcept { return op_; }

    [[nodiscard]] std::string ToString() const override;

    [[nodiscard]] std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<BinaryOpExpr>(left_ ? left_->Clone() : nullptr, op_, right_ ? right_->Clone() : nullptr);
    }

private:
    std::unique_ptr<Expression> left_;
    BinaryOp op_;
    std::unique_ptr<Expression> right_;
};

class AggregateExpr : public Expression {
public:
    AggregateExpr(AggregateType agg_type, std::unique_ptr<Expression> arg, bool is_star = false)
        : agg_type_(agg_type), arg_(std::move(arg)), is_star_(is_star) {}

    [[nodiscard]] ExprType GetType() const noexcept override { return ExprType::AGGREGATE; }
    [[nodiscard]] AggregateType GetAggregateType() const noexcept { return agg_type_; }
    [[nodiscard]] const Expression* GetArg() const noexcept { return arg_.get(); }
    [[nodiscard]] bool IsStar() const noexcept { return is_star_; }

    [[nodiscard]] std::string ToString() const override;

    [[nodiscard]] std::unique_ptr<Expression> Clone() const override {
        return std::make_unique<AggregateExpr>(agg_type_, arg_ ? arg_->Clone() : nullptr, is_star_);
    }

private:
    AggregateType agg_type_;
    std::unique_ptr<Expression> arg_;
    bool is_star_{false};
};

// Statements
enum class StatementType {
    CREATE_TABLE,
    DROP_TABLE,
    CREATE_INDEX,
    INSERT,
    SELECT,
    UPDATE,
    DELETE_STMT,
    EXPLAIN
};

class Statement {
public:
    virtual ~Statement() = default;
    [[nodiscard]] virtual StatementType GetType() const noexcept = 0;
};

struct ColumnDefinition {
    std::string name;
    DataType type{DataType::INVALID};
    bool is_nullable{true};
    bool is_primary_key{false};
};

class CreateTableStatement : public Statement {
public:
    CreateTableStatement(std::string table_name, std::vector<ColumnDefinition> columns, bool if_not_exists = false)
        : table_name_(std::move(table_name)), columns_(std::move(columns)), if_not_exists_(if_not_exists) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::CREATE_TABLE; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] const std::vector<ColumnDefinition>& GetColumns() const noexcept { return columns_; }
    [[nodiscard]] bool IfNotExists() const noexcept { return if_not_exists_; }

private:
    std::string table_name_;
    std::vector<ColumnDefinition> columns_;
    bool if_not_exists_{false};
};

class DropTableStatement : public Statement {
public:
    explicit DropTableStatement(std::string table_name, bool if_exists = false)
        : table_name_(std::move(table_name)), if_exists_(if_exists) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::DROP_TABLE; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] bool IfExists() const noexcept { return if_exists_; }

private:
    std::string table_name_;
    bool if_exists_{false};
};

class CreateIndexStatement : public Statement {
public:
    CreateIndexStatement(std::string index_name, std::string table_name, std::string column_name)
        : index_name_(std::move(index_name)), table_name_(std::move(table_name)), column_name_(std::move(column_name)) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::CREATE_INDEX; }
    [[nodiscard]] const std::string& GetIndexName() const noexcept { return index_name_; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] const std::string& GetColumnName() const noexcept { return column_name_; }

private:
    std::string index_name_;
    std::string table_name_;
    std::string column_name_;
};

class InsertStatement : public Statement {
public:
    InsertStatement(std::string table_name, std::vector<std::string> columns,
                    std::vector<std::vector<std::unique_ptr<Expression>>> values_list)
        : table_name_(std::move(table_name)), columns_(std::move(columns)), values_list_(std::move(values_list)) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::INSERT; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] const std::vector<std::string>& GetColumns() const noexcept { return columns_; }
    [[nodiscard]] const std::vector<std::vector<std::unique_ptr<Expression>>>& GetValuesList() const noexcept { return values_list_; }

private:
    std::string table_name_;
    std::vector<std::string> columns_;
    std::vector<std::vector<std::unique_ptr<Expression>>> values_list_;
};

class SelectStatement : public Statement {
public:
    SelectStatement(std::vector<std::unique_ptr<Expression>> select_list,
                    std::string from_table,
                    std::string join_table = "",
                    std::unique_ptr<Expression> join_condition = nullptr,
                    std::unique_ptr<Expression> where_clause = nullptr,
                    std::vector<std::string> group_by = {},
                    std::unique_ptr<Expression> having_clause = nullptr,
                    std::string order_by_column = "",
                    bool is_desc = false,
                    int limit = -1)
        : select_list_(std::move(select_list)),
          from_table_(std::move(from_table)),
          join_table_(std::move(join_table)),
          join_condition_(std::move(join_condition)),
          where_clause_(std::move(where_clause)),
          group_by_(std::move(group_by)),
          having_clause_(std::move(having_clause)),
          order_by_column_(std::move(order_by_column)),
          is_desc_(is_desc),
          limit_(limit) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::SELECT; }
    [[nodiscard]] const std::vector<std::unique_ptr<Expression>>& GetSelectList() const noexcept { return select_list_; }
    [[nodiscard]] const std::string& GetFromTable() const noexcept { return from_table_; }
    [[nodiscard]] const std::string& GetJoinTable() const noexcept { return join_table_; }
    [[nodiscard]] const Expression* GetJoinCondition() const noexcept { return join_condition_.get(); }
    [[nodiscard]] const Expression* GetWhereClause() const noexcept { return where_clause_.get(); }
    [[nodiscard]] const std::vector<std::string>& GetGroupBy() const noexcept { return group_by_; }
    [[nodiscard]] const Expression* GetHavingClause() const noexcept { return having_clause_.get(); }
    [[nodiscard]] const std::string& GetOrderByColumn() const noexcept { return order_by_column_; }
    [[nodiscard]] bool IsDesc() const noexcept { return is_desc_; }
    [[nodiscard]] int GetLimit() const noexcept { return limit_; }

private:
    std::vector<std::unique_ptr<Expression>> select_list_;
    std::string from_table_;
    std::string join_table_;
    std::unique_ptr<Expression> join_condition_;
    std::unique_ptr<Expression> where_clause_;
    std::vector<std::string> group_by_;
    std::unique_ptr<Expression> having_clause_;
    std::string order_by_column_;
    bool is_desc_{false};
    int limit_{-1};
};

class UpdateStatement : public Statement {
public:
    UpdateStatement(std::string table_name,
                    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> assignments,
                    std::unique_ptr<Expression> where_clause)
        : table_name_(std::move(table_name)),
          assignments_(std::move(assignments)),
          where_clause_(std::move(where_clause)) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::UPDATE; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] const std::vector<std::pair<std::string, std::unique_ptr<Expression>>>& GetAssignments() const noexcept { return assignments_; }
    [[nodiscard]] const Expression* GetWhereClause() const noexcept { return where_clause_.get(); }

private:
    std::string table_name_;
    std::vector<std::pair<std::string, std::unique_ptr<Expression>>> assignments_;
    std::unique_ptr<Expression> where_clause_;
};

class DeleteStatement : public Statement {
public:
    DeleteStatement(std::string table_name, std::unique_ptr<Expression> where_clause)
        : table_name_(std::move(table_name)), where_clause_(std::move(where_clause)) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::DELETE_STMT; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] const Expression* GetWhereClause() const noexcept { return where_clause_.get(); }

private:
    std::string table_name_;
    std::unique_ptr<Expression> where_clause_;
};

class ExplainStatement : public Statement {
public:
    explicit ExplainStatement(std::unique_ptr<Statement> statement)
        : statement_(std::move(statement)) {}

    [[nodiscard]] StatementType GetType() const noexcept override { return StatementType::EXPLAIN; }
    [[nodiscard]] const Statement* GetStatement() const noexcept { return statement_.get(); }

private:
    std::unique_ptr<Statement> statement_;
};

} // namespace ceebase
