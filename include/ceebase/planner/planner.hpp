#pragma once

#include <memory>
#include <string>

#include "ceebase/catalog/catalog.hpp"
#include "ceebase/common/result.hpp"
#include "ceebase/parser/ast.hpp"
#include "ceebase/planner/logical_plan.hpp"

namespace ceebase {

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

} // namespace ceebase
