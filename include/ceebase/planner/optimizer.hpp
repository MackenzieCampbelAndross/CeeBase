#pragma once

#include <memory>

#include "ceebase/catalog/catalog.hpp"
#include "ceebase/planner/logical_plan.hpp"

namespace ceebase {

struct PlanCost {
    double estimated_cost{0.0};
    size_t estimated_cardinality{0};
    bool used_index{false};
    std::string access_path{};
};

class Optimizer {
public:
    explicit Optimizer(Catalog* catalog) : catalog_(catalog) {}

    // Optimize a logical plan tree into an optimized execution plan
    std::unique_ptr<LogicalPlanNode> Optimize(std::unique_ptr<LogicalPlanNode> plan);

    // Calculate cost and access path for EXPLAIN output
    PlanCost EstimateCost(const LogicalPlanNode* plan) const;

private:
    std::unique_ptr<LogicalPlanNode> ApplyIndexSelection(std::unique_ptr<LogicalPlanNode> plan);

    Catalog* catalog_;
};

} // namespace ceebase
