# Risks and Assumptions

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

## Risk Register

| ID | Risk | Impact | Mitigation | Status |
|----|------|--------|------------|--------|
| R1 | Partition granularity dominates outcomes | Bad S2 level can make partitioning look worse or better than it truly is | Benchmark multiple S2 levels; include dense and sparse datasets | **Open** — s2_level=12 is hardcoded default; multi-level benchmarks not yet run |
| R2 | Boundary repair hides partition weaknesses | Impossible to tell whether partitioning itself works | Enforce partitioned-only runs before adding repair | **Mitigated** — Grid-HNSW ships without repair; clean ablation baseline exists |
| R3 | Planner fallback masks index design flaws | Smart planner hides weak partitioned index by routing around it | Require direct A2 and A3 runs without planner adaptation | **Mitigated** — benchmark forces index-specific modes (ACORN vs Grid vs B0) |
| R4 | Correlation-aware tuning adds noise too early | Program becomes overfit to synthetic assumptions | Keep correlation as optional; do not mix into acceptance of core ablations | **Mitigated** — deferred; not shipped |
| R5 | FE/BE interface churn | Planner and runtime option propagation may break across surfaces | Centralize via Thrift `TVectorSearchOptions`; test propagation separately | **Mitigated** — FE/BE interface stable; useAcorn/useGridHnsw flow proven |
| R6 | Build and compaction cost may exceed expectations | Segment-local partitioned index may be too costly to build/rebuild | Measure build time and compaction overhead | **Open** — sustained compaction testing not yet done |
| R7 | Faiss binary format changes across versions | Custom parser breaks if TenANN upgrades Faiss | M=0 recovery fallback added; diagnostic logging in parser | **Partially mitigated** — recovery exists but parser may need updates |
| R8 | Very selective predicates yield low recall | Radius <1km on sparse data leaves too few qualifying nodes for ACORN | Boost ef_search for selective predicates; document limitations | **Partially mitigated** — ef_search boosted 10x with predicates; recall ~0.74 at 1km |

## Open Decisions

1. Whether the first repair mechanism is stitching, overlap, or coarse-level fallback
2. Whether hybrid index metadata should live beside the current vector index or as a distinct index family
3. How to estimate spatial-vector correlation (rho) cheaply at compaction time for cost-model decisions
