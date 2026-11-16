#pragma once

#include "nexus/parser/ast.hpp"
#include "nexus/record/schema.hpp"
#include "nexus/record/tuple.hpp"
#include "nexus/record/value.hpp"

namespace nexus {

class ExpressionEvaluator {
public:
    static Value Evaluate(const Expression* expr, const Tuple* tuple, const Schema* schema) {
        if (!expr) {
            return Value(true); // Empty predicate evaluates to true
        }

        switch (expr->GetType()) {
            case ExprType::LITERAL: {
                auto* lit = static_cast<const LiteralExpr*>(expr);
                return lit->GetValue();
            }
            case ExprType::COLUMN_REF: {
                if (!tuple || !schema) {
                    return Value::MakeNull(DataType::INVALID);
                }
                auto* cref = static_cast<const ColumnRefExpr*>(expr);
                int idx = schema->GetColumnIndex(cref->GetColumnName());
                if (idx == -1) {
                    // Try matching with table prefix stripped or added
                    for (size_t i = 0; i < schema->GetColumnCount(); ++i) {
                        const std::string& name = schema->GetColumn(i).GetName();
                        auto dot = name.find('.');
                        if (dot != std::string::npos && name.substr(dot + 1) == cref->GetColumnName()) {
                            idx = static_cast<int>(i);
                            break;
                        }
                    }
                }
                if (idx != -1) {
                    return tuple->GetValue(*schema, static_cast<size_t>(idx));
                }
                return Value::MakeNull(DataType::INVALID);
            }
            case ExprType::BINARY_OP: {
                auto* bop = static_cast<const BinaryOpExpr*>(expr);
                Value left_val = Evaluate(bop->GetLeft(), tuple, schema);
                Value right_val = Evaluate(bop->GetRight(), tuple, schema);

                switch (bop->GetOp()) {
                    case BinaryOp::EQUALS:
                        return Value(left_val == right_val);
                    case BinaryOp::NOT_EQUALS:
                        return Value(left_val != right_val);
                    case BinaryOp::LESS:
                        return Value(left_val < right_val);
                    case BinaryOp::LESS_EQUALS:
                        return Value(left_val <= right_val);
                    case BinaryOp::GREATER:
                        return Value(left_val > right_val);
                    case BinaryOp::GREATER_EQUALS:
                        return Value(left_val >= right_val);
                    case BinaryOp::AND:
                        return Value(left_val.GetBoolean() && right_val.GetBoolean());
                    case BinaryOp::OR:
                        return Value(left_val.GetBoolean() || right_val.GetBoolean());
                    case BinaryOp::ADD: {
                        if (left_val.GetType() == DataType::INTEGER && right_val.GetType() == DataType::INTEGER) {
                            return Value(left_val.GetInteger() + right_val.GetInteger());
                        }
                        if (left_val.GetType() == DataType::DOUBLE || right_val.GetType() == DataType::DOUBLE) {
                            double l = left_val.GetType() == DataType::DOUBLE ? left_val.GetDouble() : static_cast<double>(left_val.GetInteger());
                            double r = right_val.GetType() == DataType::DOUBLE ? right_val.GetDouble() : static_cast<double>(right_val.GetInteger());
                            return Value(l + r);
                        }
                        return Value::MakeNull(DataType::INVALID);
                    }
                    default:
                        return Value::MakeNull(DataType::INVALID);
                }
            }
            case ExprType::AGGREGATE: {
                if (!tuple || !schema) {
                    return Value::MakeNull(DataType::INVALID);
                }
                std::string agg_name = expr->ToString();
                int idx = schema->GetColumnIndex(agg_name);
                if (idx != -1) {
                    return tuple->GetValue(*schema, static_cast<size_t>(idx));
                }
                return Value::MakeNull(DataType::INVALID);
            }
            default:
                return Value::MakeNull(DataType::INVALID);
        }
    }
};

} // namespace nexus
