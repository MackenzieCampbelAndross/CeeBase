#include "nexus/parser/ast.hpp"

namespace nexus {

std::string_view BinaryOpToString(BinaryOp op) noexcept {
    switch (op) {
        case BinaryOp::EQUALS: return "=";
        case BinaryOp::NOT_EQUALS: return "!=";
        case BinaryOp::LESS: return "<";
        case BinaryOp::LESS_EQUALS: return "<=";
        case BinaryOp::GREATER: return ">";
        case BinaryOp::GREATER_EQUALS: return ">=";
        case BinaryOp::AND: return "AND";
        case BinaryOp::OR: return "OR";
        case BinaryOp::ADD: return "+";
        case BinaryOp::SUBTRACT: return "-";
        case BinaryOp::MULTIPLY: return "*";
        case BinaryOp::DIVIDE: return "/";
        default: return "UNKNOWN_OP";
    }
}

std::string_view AggregateTypeToString(AggregateType type) noexcept {
    switch (type) {
        case AggregateType::COUNT: return "COUNT";
        case AggregateType::SUM: return "SUM";
        case AggregateType::AVG: return "AVG";
        case AggregateType::MIN: return "MIN";
        case AggregateType::MAX: return "MAX";
        default: return "UNKNOWN_AGG";
    }
}

std::string BinaryOpExpr::ToString() const {
    return "(" + (left_ ? left_->ToString() : "NULL") + " " +
           std::string(BinaryOpToString(op_)) + " " +
           (right_ ? right_->ToString() : "NULL") + ")";
}

std::string AggregateExpr::ToString() const {
    std::string s(AggregateTypeToString(agg_type_));
    s += "(";
    if (is_star_) {
        s += "*";
    } else if (arg_) {
        s += arg_->ToString();
    }
    s += ")";
    return s;
}

} // namespace nexus
