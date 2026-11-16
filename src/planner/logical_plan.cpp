#include "nexus/planner/logical_plan.hpp"

namespace nexus {

std::string_view PlanTypeToString(PlanType type) noexcept {
    switch (type) {
        case PlanType::SEQ_SCAN: return "SeqScan";
        case PlanType::INDEX_SCAN: return "IndexScan";
        case PlanType::FILTER: return "Filter";
        case PlanType::PROJECT: return "Projection";
        case PlanType::SORT: return "Sort";
        case PlanType::LIMIT: return "Limit";
        case PlanType::JOIN: return "NestedLoopJoin";
        case PlanType::AGGREGATE: return "Aggregate";
        default: return "UnknownPlan";
    }
}

namespace {
std::string MakeIndent(int indent) {
    return std::string(static_cast<size_t>(indent * 2), ' ');
}
} // namespace

std::string LogicalSeqScan::ToString(int indent) const {
    return MakeIndent(indent) + "-> SeqScan [Table: " + table_name_ + "]\n";
}

std::string LogicalIndexScan::ToString(int indent) const {
    return MakeIndent(indent) + "-> IndexScan [Table: " + table_name_ +
           ", Index: " + index_name_ + ", Key: " + key_col_ + " = " + std::to_string(key_val_) + "]\n";
}

std::string LogicalFilter::ToString(int indent) const {
    std::string s = MakeIndent(indent) + "-> Filter [Predicate: " + (predicate_ ? predicate_->ToString() : "TRUE") + "]\n";
    if (child_) s += child_->ToString(indent + 1);
    return s;
}

std::string LogicalProject::ToString(int indent) const {
    std::string s = MakeIndent(indent) + "-> Projection [Columns: ";
    for (size_t i = 0; i < schema_.GetColumnCount(); ++i) {
        if (i > 0) s += ", ";
        s += schema_.GetColumn(i).GetName();
    }
    s += "]\n";
    if (child_) s += child_->ToString(indent + 1);
    return s;
}

std::string LogicalSort::ToString(int indent) const {
    std::string s = MakeIndent(indent) + "-> Sort [Column: " + order_by_col_ + (is_desc_ ? " DESC" : " ASC") + "]\n";
    if (child_) s += child_->ToString(indent + 1);
    return s;
}

std::string LogicalLimit::ToString(int indent) const {
    std::string s = MakeIndent(indent) + "-> Limit [Count: " + std::to_string(limit_) + "]\n";
    if (child_) s += child_->ToString(indent + 1);
    return s;
}

std::string LogicalJoin::ToString(int indent) const {
    std::string s = MakeIndent(indent) + "-> NestedLoopJoin [Predicate: " + (predicate_ ? predicate_->ToString() : "TRUE") + "]\n";
    if (left_) s += left_->ToString(indent + 1);
    if (right_) s += right_->ToString(indent + 1);
    return s;
}

std::string LogicalAggregate::ToString(int indent) const {
    std::string s = MakeIndent(indent) + "-> Aggregate [GroupBy: ";
    for (size_t i = 0; i < group_by_.size(); ++i) {
        if (i > 0) s += ", ";
        s += group_by_[i];
    }
    s += "]\n";
    if (child_) s += child_->ToString(indent + 1);
    return s;
}

} // namespace nexus
