# Query Planning, Optimization & Execution Engine

## Overview

**NexusDB** uses an operator-based physical execution engine built upon the classical Volcano Iterator Model. Queries execute pipelined tuple streams without materializing entire intermediate tables into memory whenever possible.

---

## 1. Logical Query Plans (`LogicalPlanNode`)

A logical query plan is an abstract tree representing relational algebra operations:

- `LogicalSeqScan`: Reads all tuples from a heap table.
- `LogicalIndexScan`: Reads matching tuples via a B+ Tree index point or range scan.
- `LogicalFilter`: Evaluates a boolean predicate expression against child tuples.
- `LogicalProject`: Evaluates projection expressions and produces a mapped output schema.
- `LogicalSort`: Orders child tuples by a specified key column (ASC or DESC).
- `LogicalLimit`: Restricts output stream to $K$ tuples.
- `LogicalJoin`: Evaluates INNER JOIN between two input relations.
- `LogicalAggregate`: Computes grouping keys and aggregate functions (`COUNT`, `SUM`, `AVG`, `MIN`, `MAX`) with optional HAVING filters.

---

## 2. Query Optimizer (`nexus::Optimizer`)

The rule-based and cost-based optimizer transforms the logical plan into an optimal physical execution plan.

### 2.1 Index Selection Rule
If a `LogicalFilter` sits above a `LogicalSeqScan`, and the filter predicate is an equality comparison on an indexed column:
$$\text{Filter}(\text{column} = \text{literal}) \implies \text{IndexScan}$$
The optimizer inspects the catalog:
- If an index exists on `column`, it compares the estimated I/O cost of sequential scan vs. index point lookup.
- If $Cost(\text{IndexScan}) < Cost(\text{SeqScan})$, the optimizer rewrites the subtree into a `LogicalIndexScan`.

### 2.2 Cost Model & Cardinality Estimation
- **IndexScan Cost**: Estimated at $3.0$ I/Os (1 root page fetch + 1 leaf page fetch + 1 table heap tuple fetch). Estimated cardinality: $1$ row.
- **SeqScan Cost**: Estimated as $100.0$ I/Os (proportional to total allocated pages in the table heap). Estimated cardinality: table tuple count.
- **Filter Cost**: Sequential scan cost + predicate evaluation overhead ($55.0$ I/Os).

---

## 3. Physical Volcano Executors (`AbstractExecutor`)

Every physical executor implements the Volcano interface:

```cpp
class AbstractExecutor {
public:
    virtual void Init() = 0;
    virtual bool Next(Tuple* tuple, RID* rid) = 0;
    [[nodiscard]] virtual const Schema& GetOutputSchema() const = 0;
};
```

### Physical Operators Implemented:

1. **`SeqScanExecutor`**:
   Iterates over the `TableHeap` via `TableIterator`. Fetches page blocks on-demand through `BufferPoolManager` and advances slot-by-slot.

2. **`IndexScanExecutor`**:
   Performs $O(\log N)$ point lookup in the `BPlusTreeIndex` to retrieve the tuple `RID`, then fetches the single exact page from the `TableHeap`.

3. **`FilterExecutor`**:
   Wraps a child executor and pulls tuples until `ExpressionEvaluator::Evaluate()` returns `true`.

4. **`SortExecutor`**:
   Pulls all tuples from child, performs an in-memory sort using `std::stable_sort` on the specified column, and streams out in sorted order.

5. **`LimitExecutor`**:
   Counts emitted tuples and halts stream once the limit is reached.

6. **`NestedLoopJoinExecutor`**:
   Implements an inner-outer nested loop join:
   - Outer loop pulls from left child executor.
   - Inner loop scans right child executor.
   - Tuples are concatenated into a joined tuple when `join_predicate` evaluates to true.

7. **`AggregationExecutor`**:
   Uses an in-memory hash table mapping `group_by_keys` to aggregate accumulators:
   - Pulls all child tuples during `Init()`.
   - Accumulates values into `AggregateAccumulator` structs (`count`, `sum`, `min_val`, `max_val`).
   - Evaluates HAVING predicates and emits final aggregated rows during `Next()`.

---

## 4. EXPLAIN Command

The `EXPLAIN` statement outputs a formatted summary of the query compilation lifecycle:

```sql
nexus> EXPLAIN SELECT * FROM employees WHERE id = 10;
```

Output:
```
=== NexusDB Query Execution Plan ===

[1. Logical Query Plan]:
  -> Filter [Predicate: (id = 10)]
    -> SeqScan [Table: employees]

[2. Physical Execution Plan]:
  -> IndexScan [Table: employees, Index: employees_pk_idx, Key: id = 10]

[3. Optimizer Diagnostics]:
  Selected Access Path: IndexScan [Index: employees_pk_idx, Key: id = 10]
  Index Used:           YES
  Estimated I/O Cost:   3
  Est. Cardinality:     1 row(s)

Execution time: 0.082 ms
```
