# Phase 05 Design Notes

## Planner Philosophy

The planner must treat spatial predicates as a structured signal, not a random filter predicate.

That means strategy selection should rely first on:

- spatial selectivity
- partition fan-out expectations
- whether repair is available

and only later on:

- optional correlation-aware hints

## Primary Reference Files

- `fe/fe-core/src/main/java/com/starrocks/sql/optimizer/rule/transformation/RewriteToVectorPlanRule.java`
- `fe/fe-core/src/main/java/com/starrocks/common/VectorSearchOptions.java`
- `fe/fe-core/src/main/java/com/starrocks/planner/OlapScanNode.java`
- `gensrc/thrift/PlanNodes.thrift`
- `research/03_algorithmic_design/cost_model.md`
- `research/03_algorithmic_design/spatial_vector_correlation.md`

## Strategy Table To Formalize

- brute-force on filtered subset
- fallback baseline path
- partitioned HNSW only
- partitioned HNSW with boundary repair

## Optional Future Hook

If correlation statistics or user hints later exist, they should:

- bias strategy choice
- never replace the selectivity-based baseline
- remain off by default until `A4` is stable

## Rejected Alternatives

### Rejected: rely entirely on BE runtime heuristics

Reason:

- hard to explain
- hard to test
- undermines planner-focused ablation

### Rejected: ship correlation-aware tuning in the same phase as baseline planner choice

Reason:

- makes `A4` and `A5` indistinguishable

## Output Expectations

- a planner decision table with visible thresholds
- a clear hook point for optional correlation-aware tuning
- a stable distinction between `A4` and `A5`
