#pragma once

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "ceebase/buffer/buffer_pool_manager.hpp"
#include "ceebase/catalog/catalog.hpp"
#include "ceebase/execution/expression_evaluator.hpp"
#include "ceebase/index/b_plus_tree.hpp"
#include "ceebase/record/schema.hpp"
#include "ceebase/record/table_heap.hpp"
#include "ceebase/record/tuple.hpp"
#include "ceebase/record/value.hpp"

namespace ceebase {

class ExecutionContext {
public:
    ExecutionContext(BufferPoolManager* bpm, Catalog* catalog)
        : bpm_(bpm), catalog_(catalog) {}

    [[nodiscard]] BufferPoolManager* GetBufferPoolManager() const noexcept { return bpm_; }
    [[nodiscard]] Catalog* GetCatalog() const noexcept { return catalog_; }

    void IncRowsScanned() noexcept { rows_scanned_++; }
    void IncIndexLookups() noexcept { index_lookups_++; }
    [[nodiscard]] size_t GetRowsScanned() const noexcept { return rows_scanned_; }
    [[nodiscard]] size_t GetIndexLookups() const noexcept { return index_lookups_; }

private:
    BufferPoolManager* bpm_;
    Catalog* catalog_;
    size_t rows_scanned_{0};
    size_t index_lookups_{0};
};

class AbstractExecutor {
public:
    explicit AbstractExecutor(ExecutionContext* exec_ctx) : exec_ctx_(exec_ctx) {}
    virtual ~AbstractExecutor() = default;

    virtual void Init() = 0;
    virtual bool Next(Tuple* tuple, RID* rid) = 0;
    [[nodiscard]] virtual const Schema& GetOutputSchema() const = 0;

    [[nodiscard]] ExecutionContext* GetExecutionContext() const noexcept { return exec_ctx_; }

protected:
    ExecutionContext* exec_ctx_;
};

// Sequential Scan Executor
class SeqScanExecutor : public AbstractExecutor {
public:
    SeqScanExecutor(ExecutionContext* exec_ctx, TableMetadata* table_meta)
        : AbstractExecutor(exec_ctx), table_meta_(table_meta), cursor_(nullptr, RID()) {}

    void Init() override {
        cursor_ = table_meta_->table_heap->Begin();
    }

    bool Next(Tuple* tuple, RID* rid) override {
        if (cursor_ != table_meta_->table_heap->End()) {
            *tuple = *cursor_;
            *rid = cursor_->GetRID();
            exec_ctx_->IncRowsScanned();
            ++cursor_;
            return true;
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return table_meta_->schema;
    }

private:
    TableMetadata* table_meta_;
    TableIterator cursor_;
};

// Index Scan Executor
class IndexScanExecutor : public AbstractExecutor {
public:
    IndexScanExecutor(ExecutionContext* exec_ctx, TableMetadata* table_meta,
                      IndexMetadata* index_meta, int64_t search_key)
        : AbstractExecutor(exec_ctx),
          table_meta_(table_meta),
          index_meta_(index_meta),
          search_key_(search_key) {}

    void Init() override {
        executed_ = false;
        rid_ = RID();
        exec_ctx_->IncIndexLookups();
        (void)index_meta_->index->GetValue(search_key_, &rid_);
    }

    bool Next(Tuple* tuple, RID* rid) override {
        if (!executed_ && rid_.IsValid()) {
            auto fetch_res = table_meta_->table_heap->GetTuple(rid_);
            if (fetch_res.ok()) {
                *tuple = *fetch_res;
                *rid = rid_;
                exec_ctx_->IncRowsScanned();
                executed_ = true;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return table_meta_->schema;
    }

private:
    TableMetadata* table_meta_;
    IndexMetadata* index_meta_;
    int64_t search_key_;
    RID rid_{};
    bool executed_{false};
};

// Filter Executor
class FilterExecutor : public AbstractExecutor {
public:
    FilterExecutor(ExecutionContext* exec_ctx, std::unique_ptr<AbstractExecutor> child,
                   const Expression* predicate)
        : AbstractExecutor(exec_ctx), child_(std::move(child)), predicate_(predicate) {}

    void Init() override {
        child_->Init();
    }

    bool Next(Tuple* tuple, RID* rid) override {
        Tuple child_tuple;
        RID child_rid;
        while (child_->Next(&child_tuple, &child_rid)) {
            Value res = ExpressionEvaluator::Evaluate(predicate_, &child_tuple, &child_->GetOutputSchema());
            if (!res.IsNull() && res.GetBoolean()) {
                *tuple = std::move(child_tuple);
                *rid = child_rid;
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return child_->GetOutputSchema();
    }

private:
    std::unique_ptr<AbstractExecutor> child_;
    const Expression* predicate_;
};

// Projection Executor
class ProjectionExecutor : public AbstractExecutor {
public:
    ProjectionExecutor(ExecutionContext* exec_ctx, std::unique_ptr<AbstractExecutor> child,
                       std::vector<const Expression*> exprs, Schema output_schema)
        : AbstractExecutor(exec_ctx),
          child_(std::move(child)),
          exprs_(std::move(exprs)),
          output_schema_(std::move(output_schema)) {}

    void Init() override {
        child_->Init();
    }

    bool Next(Tuple* tuple, RID* rid) override {
        Tuple child_tuple;
        RID child_rid;
        if (child_->Next(&child_tuple, &child_rid)) {
            std::vector<Value> vals;
            vals.reserve(exprs_.size());
            for (const auto* e : exprs_) {
                vals.push_back(ExpressionEvaluator::Evaluate(e, &child_tuple, &child_->GetOutputSchema()));
            }
            *tuple = Tuple(vals, output_schema_);
            *rid = child_rid;
            return true;
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return output_schema_;
    }

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<const Expression*> exprs_;
    Schema output_schema_;
};

// Sort Executor
class SortExecutor : public AbstractExecutor {
public:
    SortExecutor(ExecutionContext* exec_ctx, std::unique_ptr<AbstractExecutor> child,
                 std::string sort_col, bool is_desc)
        : AbstractExecutor(exec_ctx),
          child_(std::move(child)),
          sort_col_(std::move(sort_col)),
          is_desc_(is_desc) {}

    void Init() override {
        child_->Init();
        tuples_.clear();
        cursor_ = 0;

        Tuple t;
        RID r;
        while (child_->Next(&t, &r)) {
            tuples_.emplace_back(std::move(t), r);
        }

        const auto& schema = child_->GetOutputSchema();
        int col_idx = schema.GetColumnIndex(sort_col_);

        if (col_idx != -1) {
            std::stable_sort(tuples_.begin(), tuples_.end(),
                [&schema, col_idx, this](const std::pair<Tuple, RID>& a, const std::pair<Tuple, RID>& b) {
                    Value va = a.first.GetValue(schema, static_cast<size_t>(col_idx));
                    Value vb = b.first.GetValue(schema, static_cast<size_t>(col_idx));
                    if (is_desc_) {
                        return vb < va;
                    }
                    return va < vb;
                });
        }
    }

    bool Next(Tuple* tuple, RID* rid) override {
        if (cursor_ < tuples_.size()) {
            *tuple = tuples_[cursor_].first;
            *rid = tuples_[cursor_].second;
            cursor_++;
            return true;
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return child_->GetOutputSchema();
    }

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::string sort_col_;
    bool is_desc_;
    std::vector<std::pair<Tuple, RID>> tuples_{};
    size_t cursor_{0};
};

// Limit Executor
class LimitExecutor : public AbstractExecutor {
public:
    LimitExecutor(ExecutionContext* exec_ctx, std::unique_ptr<AbstractExecutor> child, size_t limit)
        : AbstractExecutor(exec_ctx), child_(std::move(child)), limit_(limit) {}

    void Init() override {
        child_->Init();
        emitted_ = 0;
    }

    bool Next(Tuple* tuple, RID* rid) override {
        if (emitted_ < limit_ && child_->Next(tuple, rid)) {
            emitted_++;
            return true;
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return child_->GetOutputSchema();
    }

private:
    std::unique_ptr<AbstractExecutor> child_;
    size_t limit_;
    size_t emitted_{0};
};

// Nested Loop Join Executor (INNER JOIN)
class NestedLoopJoinExecutor : public AbstractExecutor {
public:
    NestedLoopJoinExecutor(ExecutionContext* exec_ctx,
                           std::unique_ptr<AbstractExecutor> left,
                           std::unique_ptr<AbstractExecutor> right,
                           const Expression* predicate,
                           Schema output_schema)
        : AbstractExecutor(exec_ctx),
          left_(std::move(left)),
          right_(std::move(right)),
          predicate_(predicate),
          output_schema_(std::move(output_schema)) {}

    void Init() override {
        left_->Init();
        right_->Init();
        has_left_ = left_->Next(&cur_left_tuple_, &cur_left_rid_);
    }

    bool Next(Tuple* tuple, RID* rid) override {
        while (has_left_) {
            Tuple right_tuple;
            RID right_rid;
            while (right_->Next(&right_tuple, &right_rid)) {
                // Merge tuples
                std::vector<Value> merged_vals;
                for (size_t i = 0; i < left_->GetOutputSchema().GetColumnCount(); ++i) {
                    merged_vals.push_back(cur_left_tuple_.GetValue(left_->GetOutputSchema(), i));
                }
                for (size_t i = 0; i < right_->GetOutputSchema().GetColumnCount(); ++i) {
                    merged_vals.push_back(right_tuple.GetValue(right_->GetOutputSchema(), i));
                }
                Tuple merged_tuple(merged_vals, output_schema_);

                if (!predicate_) {
                    *tuple = std::move(merged_tuple);
                    *rid = cur_left_rid_;
                    return true;
                }

                Value cond = ExpressionEvaluator::Evaluate(predicate_, &merged_tuple, &output_schema_);
                if (!cond.IsNull() && cond.GetBoolean()) {
                    *tuple = std::move(merged_tuple);
                    *rid = cur_left_rid_;
                    return true;
                }
            }

            // Reset right executor and advance left
            right_->Init();
            has_left_ = left_->Next(&cur_left_tuple_, &cur_left_rid_);
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return output_schema_;
    }

private:
    std::unique_ptr<AbstractExecutor> left_;
    std::unique_ptr<AbstractExecutor> right_;
    const Expression* predicate_;
    Schema output_schema_;
    Tuple cur_left_tuple_{};
    RID cur_left_rid_{};
    bool has_left_{false};
};

// Aggregation Executor (GROUP BY, COUNT, SUM, AVG, MIN, MAX, HAVING)
class AggregationExecutor : public AbstractExecutor {
public:
    struct AggregateValue {
        int64_t count{0};
        double sum{0.0};
        double min_val{1e18};
        double max_val{-1e18};
        bool has_values{false};
    };

    AggregationExecutor(ExecutionContext* exec_ctx, std::unique_ptr<AbstractExecutor> child,
                        std::vector<std::string> group_by_cols,
                        std::vector<std::pair<std::string, AggregateType>> agg_exprs,
                        const Expression* having_clause,
                        Schema output_schema)
        : AbstractExecutor(exec_ctx),
          child_(std::move(child)),
          group_by_cols_(std::move(group_by_cols)),
          agg_exprs_(std::move(agg_exprs)),
          having_clause_(having_clause),
          output_schema_(std::move(output_schema)) {}

    void Init() override {
        child_->Init();
        groups_.clear();
        cursor_ = 0;
        result_tuples_.clear();

        Tuple t;
        RID r;
        const auto& child_schema = child_->GetOutputSchema();

        while (child_->Next(&t, &r)) {
            std::string group_key = "";
            for (const auto& gcol : group_by_cols_) {
                int c_idx = child_schema.GetColumnIndex(gcol);
                if (c_idx != -1) {
                    group_key += t.GetValue(child_schema, static_cast<size_t>(c_idx)).ToString() + "\t";
                }
            }

            if (groups_.find(group_key) == groups_.end()) {
                groups_[group_key] = std::vector<AggregateValue>(agg_exprs_.size());
            }

            auto& state_vec = groups_[group_key];
            for (size_t i = 0; i < agg_exprs_.size(); ++i) {
                const auto& [col_name, type] = agg_exprs_[i];
                state_vec[i].count++;

                if (col_name != "*") {
                    int c_idx = child_schema.GetColumnIndex(col_name);
                    if (c_idx != -1) {
                        Value v = t.GetValue(child_schema, static_cast<size_t>(c_idx));
                        if (!v.IsNull()) {
                            double num = 0.0;
                            if (v.GetType() == DataType::INTEGER) num = v.GetInteger();
                            else if (v.GetType() == DataType::BIGINT) num = static_cast<double>(v.GetBigInt());
                            else if (v.GetType() == DataType::DOUBLE) num = v.GetDouble();
                            else if (v.GetType() == DataType::FLOAT) num = v.GetFloat();

                            state_vec[i].sum += num;
                            state_vec[i].min_val = std::min(state_vec[i].min_val, num);
                            state_vec[i].max_val = std::max(state_vec[i].max_val, num);
                            state_vec[i].has_values = true;
                        }
                    }
                }
            }
        }

        // If no rows and no group by, generate 1 row for pure aggregates like COUNT(*)
        if (groups_.empty() && group_by_cols_.empty() && !agg_exprs_.empty()) {
            groups_[""] = std::vector<AggregateValue>(agg_exprs_.size());
        }

        // Materialize result tuples
        for (const auto& [grp_key, state_vec] : groups_) {
            std::vector<Value> vals;

            // Extract group by values from key
            size_t start = 0;
            for (size_t g = 0; g < group_by_cols_.size(); ++g) {
                size_t tab = grp_key.find('\t', start);
                std::string token = (tab != std::string::npos) ? grp_key.substr(start, tab - start) : "";
                vals.emplace_back(token);
                start = tab + 1;
            }

            // Populate aggregate results
            for (size_t i = 0; i < agg_exprs_.size(); ++i) {
                const auto& [_, type] = agg_exprs_[i];
                const auto& st = state_vec[i];
                switch (type) {
                    case AggregateType::COUNT:
                        vals.emplace_back(st.count);
                        break;
                    case AggregateType::SUM:
                        vals.emplace_back(st.sum);
                        break;
                    case AggregateType::AVG:
                        vals.emplace_back(st.count > 0 ? (st.sum / static_cast<double>(st.count)) : 0.0);
                        break;
                    case AggregateType::MIN:
                        vals.emplace_back(st.has_values ? st.min_val : 0.0);
                        break;
                    case AggregateType::MAX:
                        vals.emplace_back(st.has_values ? st.max_val : 0.0);
                        break;
                }
            }

            Tuple res_tuple(vals, output_schema_);
            if (!having_clause_) {
                result_tuples_.push_back(std::move(res_tuple));
            } else {
                Value hav = ExpressionEvaluator::Evaluate(having_clause_, &res_tuple, &output_schema_);
                if (!hav.IsNull() && hav.GetBoolean()) {
                    result_tuples_.push_back(std::move(res_tuple));
                }
            }
        }
    }

    bool Next(Tuple* tuple, RID* rid) override {
        if (cursor_ < result_tuples_.size()) {
            *tuple = result_tuples_[cursor_++];
            *rid = RID();
            return true;
        }
        return false;
    }

    [[nodiscard]] const Schema& GetOutputSchema() const override {
        return output_schema_;
    }

private:
    std::unique_ptr<AbstractExecutor> child_;
    std::vector<std::string> group_by_cols_;
    std::vector<std::pair<std::string, AggregateType>> agg_exprs_;
    const Expression* having_clause_;
    Schema output_schema_;
    std::map<std::string, std::vector<AggregateValue>> groups_{};
    std::vector<Tuple> result_tuples_{};
    size_t cursor_{0};
};

} // namespace ceebase
