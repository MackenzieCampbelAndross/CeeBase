#pragma once

#include <memory>
#include <string>

#include "nexus/catalog/catalog.hpp"
#include "nexus/common/result.hpp"
#include "nexus/parser/ast.hpp"
#include "nexus/planner/logical_plan.hpp"

namespace nexus {

class Planner {
public:
    explicit Planner(Catalog* catalog) : catalog_(catalog) {}

    // Semantic Validation
    Status ValidateStatement(const Statement* stmt);

    // Plan Generation
    Result<std::unique_ptr<LogicalPlanNode>> PlanSelect(const SelectStatement* stmt);

private:
    Status ValidateExpression(const Expression* expr, const Schema& schema);

    Catalog* catalog_;
};

} // namespace nexus
