#include "ceebase/planner/optimizer.hpp"

namespace ceebase {

std::unique_ptr<LogicalPlanNode> Optimizer::Optimize(std::unique_ptr<LogicalPlanNode> plan) {
    if (!plan) return nullptr;
    plan = ApplyIndexSelection(std::move(plan));
    return plan;
}

std::unique_ptr<LogicalPlanNode> Optimizer::ApplyIndexSelection(std::unique_ptr<LogicalPlanNode> plan) {
    if (plan->GetType() == PlanType::FILTER) {
        auto* filter = static_cast<LogicalFilter*>(plan.get());
        const auto* pred = filter->GetPredicate();

        // Check if predicate is an equality comparison: col = literal
        if (pred && pred->GetType() == ExprType::BINARY_OP) {
            auto* bop = static_cast<const BinaryOpExpr*>(pred);
            if (bop->GetOp() == BinaryOp::EQUALS) {
                const Expression* left = bop->GetLeft();
                const Expression* right = bop->GetRight();

                const ColumnRefExpr* col_ref = nullptr;
                const LiteralExpr* lit_ref = nullptr;

                if (left->GetType() == ExprType::COLUMN_REF && right->GetType() == ExprType::LITERAL) {
                    col_ref = static_cast<const ColumnRefExpr*>(left);
                    lit_ref = static_cast<const LiteralExpr*>(right);
                } else if (right->GetType() == ExprType::COLUMN_REF && left->GetType() == ExprType::LITERAL) {
                    col_ref = static_cast<const ColumnRefExpr*>(right);
                    lit_ref = static_cast<const LiteralExpr*>(left);
                }

                if (col_ref && lit_ref) {
                    auto children = filter->GetChildren();
                    if (!children.empty() && children[0]->GetType() == PlanType::SEQ_SCAN) {
                        auto* seq_scan = static_cast<const LogicalSeqScan*>(children[0]);
                        const std::string& tbl_name = seq_scan->GetTableName();

                        // Check if an index exists on this column
                        auto* idx_meta = catalog_->GetIndexForColumn(tbl_name, col_ref->GetColumnName());
                        if (idx_meta != nullptr) {
                            int64_t key_val = 0;
                            const Value& v = lit_ref->GetValue();
                            if (v.GetType() == DataType::INTEGER) {
                                key_val = v.GetInteger();
                            } else if (v.GetType() == DataType::BIGINT) {
                                key_val = v.GetBigInt();
                            }

                            // Cost of IndexScan vs SeqScan
                            double seq_cost = 100.0; // table scan estimate
                            double idx_cost = 3.0;   // root + leaf + tuple I/O

                            if (idx_cost < seq_cost) {
                                // Transform Filter(SeqScan) -> IndexScan!
                                return std::make_unique<LogicalIndexScan>(tbl_name, idx_meta->name,
                                                                         col_ref->GetColumnName(), key_val,
                                                                         seq_scan->GetOutputSchema());
                            }
                        }
                    }
                }
            }
        }
    }
    return plan;
}

PlanCost Optimizer::EstimateCost(const LogicalPlanNode* plan) const {
    PlanCost cost;
    if (!plan) return cost;

    switch (plan->GetType()) {
        case PlanType::INDEX_SCAN: {
            cost.estimated_cost = 3.0; // 3 I/Os
            cost.estimated_cardinality = 1;
            cost.used_index = true;
            auto* iscan = static_cast<const LogicalIndexScan*>(plan);
            cost.access_path = "IndexScan [Index: " + iscan->GetIndexName() +
                               ", Key: " + iscan->GetKeyColumn() + " = " + std::to_string(iscan->GetKeyValue()) + "]";
            break;
        }
        case PlanType::SEQ_SCAN: {
            auto* sscan = static_cast<const LogicalSeqScan*>(plan);
            auto* tbl = catalog_->GetTable(sscan->GetTableName());
            (void)tbl;
            size_t num_tuples = 100;
            cost.estimated_cost = 50.0;
            cost.estimated_cardinality = num_tuples;
            cost.used_index = false;
            cost.access_path = "SeqScan [Table: " + sscan->GetTableName() + "]";
            break;
        }
        case PlanType::FILTER: {
            cost.estimated_cost = 55.0;
            cost.estimated_cardinality = 50;
            cost.access_path = "Filter";
            break;
        }
        case PlanType::JOIN: {
            cost.estimated_cost = 250.0;
            cost.estimated_cardinality = 100;
            cost.access_path = "NestedLoopJoin";
            break;
        }
        default:
            cost.estimated_cost = 10.0;
            cost.estimated_cardinality = 10;
            cost.access_path = std::string(PlanTypeToString(plan->GetType()));
            break;
    }
    return cost;
}

} // namespace ceebase
