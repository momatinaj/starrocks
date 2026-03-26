# Phase 02 Design Notes

## Design Intent

This phase intentionally avoids a new index. Its role is to define the best possible baseline that:

- understands the query shape
- can exploit spatial-first execution
- can later be compared against partitioned index work

## Primary Reference Files

- `fe/fe-core/src/main/java/com/starrocks/sql/optimizer/rule/transformation/RewriteToVectorPlanRule.java`
- `fe/fe-core/src/main/java/com/starrocks/common/VectorSearchOptions.java`
- `fe/fe-core/src/main/java/com/starrocks/planner/OlapScanNode.java`
- `gensrc/thrift/PlanNodes.thrift`
- `be/src/exec/pipeline/scan/olap_chunk_source.cpp`
- `be/src/storage/rowset/segment_iterator.cpp`

## Intended Fallback Modes

### Mode F1: Spatial Filter + Exact Distance

Use when the spatial predicate is highly selective and candidate count is very small.

### Mode F2: Spatial Filter + Existing ANN Reuse

Use when the filtered set is large enough to benefit from ANN behavior but still lacks a dedicated partitioned index.

### Mode F3: Existing Vector Path

Retain as a fallback-of-last-resort for very broad spatial predicates where spatial filtering barely reduces the search space.

## Why This Phase Matters

Without this phase:

- the first partitioned-index benchmark might accidentally compare against an unrealistically weak baseline
- planner and runtime responsibilities would remain undefined

## Rejected Alternatives

### Rejected: start directly with the partitioned index MVP

Reason:

- no clean baseline for measuring actual partitioning value
- high risk of conflating query-shape recognition with index benefits

### Rejected: implement planner adaptation before baseline fallback

Reason:

- planner choice is meaningful only after a baseline execution path exists

## Output Expectations

This phase should leave behind:

- a clear fallback design
- no ambiguity around `B2`
- FE/BE option boundaries that later phases can extend
