#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ceebase/catalog/catalog.hpp"
#include "ceebase/parser/ast.hpp"
#include "ceebase/record/schema.hpp"

namespace ceebase {

enum class PlanType {
    SEQ_SCAN,
    INDEX_SCAN,
    FILTER,
    PROJECT,
    SORT,
    LIMIT,
    JOIN,
    AGGREGATE
};

[[nodiscard]] std::string_view PlanTypeToString(PlanType type) noexcept;

class LogicalPlanNode {
public:
    virtual ~LogicalPlanNode() = default;
    [[nodiscard]] virtual PlanType GetType() const noexcept = 0;
    [[nodiscard]] virtual const Schema& GetOutputSchema() const noexcept = 0;
    [[nodiscard]] virtual std::vector<const LogicalPlanNode*> GetChildren() const = 0;
    [[nodiscard]] virtual std::string ToString(int indent = 0) const = 0;
};

class LogicalSeqScan : public LogicalPlanNode {
public:
    LogicalSeqScan(std::string table_name, Schema schema)
        : table_name_(std::move(table_name)), schema_(std::move(schema)) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::SEQ_SCAN; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return schema_; }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {}; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::string table_name_;
    Schema schema_;
};

class LogicalIndexScan : public LogicalPlanNode {
public:
    LogicalIndexScan(std::string table_name, std::string index_name, std::string key_col,
                     int64_t key_val, Schema schema)
        : table_name_(std::move(table_name)),
          index_name_(std::move(index_name)),
          key_col_(std::move(key_col)),
          key_val_(key_val),
          schema_(std::move(schema)) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::INDEX_SCAN; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return schema_; }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {}; }
    [[nodiscard]] const std::string& GetTableName() const noexcept { return table_name_; }
    [[nodiscard]] const std::string& GetIndexName() const noexcept { return index_name_; }
    [[nodiscard]] const std::string& GetKeyColumn() const noexcept { return key_col_; }
    [[nodiscard]] int64_t GetKeyValue() const noexcept { return key_val_; }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::string table_name_;
    std::string index_name_;
    std::string key_col_;
    int64_t key_val_{0};
    Schema schema_;
};

class LogicalFilter : public LogicalPlanNode {
public:
    LogicalFilter(std::unique_ptr<LogicalPlanNode> child, std::unique_ptr<Expression> predicate)
        : child_(std::move(child)), predicate_(std::move(predicate)) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::FILTER; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return child_->GetOutputSchema(); }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {child_.get()}; }
    [[nodiscard]] const Expression* GetPredicate() const noexcept { return predicate_.get(); }
    [[nodiscard]] std::unique_ptr<LogicalPlanNode> TransferChild() { return std::move(child_); }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::unique_ptr<LogicalPlanNode> child_;
    std::unique_ptr<Expression> predicate_;
};

class LogicalProject : public LogicalPlanNode {
public:
    LogicalProject(std::unique_ptr<LogicalPlanNode> child,
                   std::vector<std::unique_ptr<Expression>> exprs,
                   Schema output_schema)
        : child_(std::move(child)), exprs_(std::move(exprs)), schema_(std::move(output_schema)) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::PROJECT; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return schema_; }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {child_.get()}; }
    [[nodiscard]] const std::vector<std::unique_ptr<Expression>>& GetExpressions() const noexcept { return exprs_; }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::unique_ptr<LogicalPlanNode> child_;
    std::vector<std::unique_ptr<Expression>> exprs_;
    Schema schema_;
};

class LogicalSort : public LogicalPlanNode {
public:
    LogicalSort(std::unique_ptr<LogicalPlanNode> child, std::string order_by_column, bool is_desc)
        : child_(std::move(child)), order_by_col_(std::move(order_by_column)), is_desc_(is_desc) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::SORT; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return child_->GetOutputSchema(); }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {child_.get()}; }
    [[nodiscard]] const std::string& GetOrderByColumn() const noexcept { return order_by_col_; }
    [[nodiscard]] bool IsDesc() const noexcept { return is_desc_; }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::unique_ptr<LogicalPlanNode> child_;
    std::string order_by_col_;
    bool is_desc_{false};
};

class LogicalLimit : public LogicalPlanNode {
public:
    LogicalLimit(std::unique_ptr<LogicalPlanNode> child, int limit)
        : child_(std::move(child)), limit_(limit) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::LIMIT; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return child_->GetOutputSchema(); }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {child_.get()}; }
    [[nodiscard]] int GetLimit() const noexcept { return limit_; }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::unique_ptr<LogicalPlanNode> child_;
    int limit_{-1};
};

class LogicalJoin : public LogicalPlanNode {
public:
    LogicalJoin(std::unique_ptr<LogicalPlanNode> left, std::unique_ptr<LogicalPlanNode> right,
                std::unique_ptr<Expression> predicate, Schema output_schema)
        : left_(std::move(left)), right_(std::move(right)), predicate_(std::move(predicate)), schema_(std::move(output_schema)) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::JOIN; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return schema_; }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {left_.get(), right_.get()}; }
    [[nodiscard]] const Expression* GetPredicate() const noexcept { return predicate_.get(); }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::unique_ptr<LogicalPlanNode> left_;
    std::unique_ptr<LogicalPlanNode> right_;
    std::unique_ptr<Expression> predicate_;
    Schema schema_;
};

class LogicalAggregate : public LogicalPlanNode {
public:
    LogicalAggregate(std::unique_ptr<LogicalPlanNode> child,
                     std::vector<std::string> group_by,
                     std::vector<std::pair<std::string, AggregateType>> aggregates,
                     std::unique_ptr<Expression> having,
                     Schema output_schema)
        : child_(std::move(child)),
          group_by_(std::move(group_by)),
          aggregates_(std::move(aggregates)),
          having_(std::move(having)),
          schema_(std::move(output_schema)) {}

    [[nodiscard]] PlanType GetType() const noexcept override { return PlanType::AGGREGATE; }
    [[nodiscard]] const Schema& GetOutputSchema() const noexcept override { return schema_; }
    [[nodiscard]] std::vector<const LogicalPlanNode*> GetChildren() const override { return {child_.get()}; }
    [[nodiscard]] const std::vector<std::string>& GetGroupBy() const noexcept { return group_by_; }
    [[nodiscard]] const std::vector<std::pair<std::string, AggregateType>>& GetAggregates() const noexcept { return aggregates_; }
    [[nodiscard]] const Expression* GetHaving() const noexcept { return having_.get(); }
    [[nodiscard]] std::string ToString(int indent = 0) const override;

private:
    std::unique_ptr<LogicalPlanNode> child_;
    std::vector<std::string> group_by_;
    std::vector<std::pair<std::string, AggregateType>> aggregates_;
    std::unique_ptr<Expression> having_;
    Schema schema_;
};

} // namespace ceebase
