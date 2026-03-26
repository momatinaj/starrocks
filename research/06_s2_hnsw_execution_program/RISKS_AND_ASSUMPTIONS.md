# Risks And Assumptions

## Assumptions

### Product assumptions

- The primary query pattern is spatial filter + ANN top-k.
- StarRocks should optimize spatial predicates as a special class, not as generic filter predicates.
- Segment-local immutable index construction is the right storage fit for the hybrid index.

### Technical assumptions

- Existing TenANN-based HNSW can be reused for per-partition ANN rather than replaced immediately.
- S2 is the preferred spatial partitioning system because it already exists in StarRocks' geo stack.
- Cross-partition stitching can be implemented without invalidating the core partition-only ablation.

### Program assumptions

- Benchmarking and correctness work will evolve in parallel with implementation planning.
- Correlation-aware tuning remains optional until the non-correlation phases are stable.

## Major Risks

### R1: Partition granularity dominates outcomes

Impact:

- A bad S2 level can make partitioning look worse or better than it truly is.

Mitigation:

- benchmark multiple S2 levels
- include dense and sparse datasets
- keep `A2` independent from repair and planner logic

### R2: Boundary repair hides partition weaknesses

Impact:

- If introduced too early, it becomes impossible to tell whether partitioning itself works.

Mitigation:

- enforce `A2` before `A3`
- benchmark boundary-heavy and interior-heavy queries separately

### R3: Planner fallback masks index design flaws

Impact:

- A smart planner can hide a weak partitioned index by routing around it too often.

Mitigation:

- require direct `A2` and `A3` runs without planner adaptation
- compare planner-selected vs forced strategy execution

### R4: Correlation-aware tuning adds noise too early

Impact:

- The program may become overfit to synthetic assumptions before core mechanics stabilize.

Mitigation:

- keep correlation-aware logic as optional `A5`
- do not mix `A5` into acceptance of `A2` to `A4`

### R5: FE/BE interface churn

Impact:

- planner and runtime option propagation may require multiple surfaces to change together

Mitigation:

- centralize FE/BE interface assumptions in the phase docs
- test explainability and thrift propagation separately from index behavior

### R6: Build and compaction cost may exceed expectations

Impact:

- a segment-local partitioned index may be query-fast but too costly to build or rebuild

Mitigation:

- measure build time and compaction overhead in every relevant phase
- keep tiny-partition brute-force fallback in scope

## Open Decisions

1. Whether the first repair mechanism is stitching, overlap, or coarse-level fallback
2. Whether FE adaptation should extend `RewriteToVectorPlanRule` or create a dedicated hybrid rule
3. Whether hybrid index metadata should live beside the current vector index or as a distinct index family

## Review Gates

### Gate 1: End of Phase 1

- ablation contract accepted
- benchmark axes fixed
- no ambiguity about baseline definitions

### Gate 2: End of Phase 3

- partitioned MVP works and is benchmarkable
- decision retained on whether repair remains the next phase

### Gate 3: End of Phase 4

- boundary repair delivers measurable recall value
- repair cost is acceptable enough to justify planner integration

### Gate 4: End of Phase 5

- planner fallback helps more often than it hurts
- strategy visibility is sufficient for debugging and SQL testing

### Gate 5: End of Phase 6

- package is benchmarked, regression-checked, and ready for implementation iteration beyond planning
