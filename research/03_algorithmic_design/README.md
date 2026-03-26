# Algorithmic Design

## Overview

This section proposes and analyzes candidate hybrid index designs that combine spatial filtering with K-ANN vector search. Each approach is informed by the literature survey (Iteration 1) and industry survey (Iteration 2).

## Design Space

The fundamental tension in hybrid spatial+vector search is:

```
Spatial-first:  Filter by region → ANN on survivors
                + Exact spatial results
                - Survivors may be too many for brute-force or too few for graph traversal

Vector-first:   ANN search → Filter by region
                + Uses ANN index efficiently
                - Must oversample K (10-100x) to get K results after filtering

Hybrid:         Interleaved spatial and vector processing
                + Balances both concerns
                - Complex implementation, cost model required
```

## Spatial–Vector Correlation (ρ)

Independently of selectivity, **how aligned** are geographic proximity and embedding similarity?

| ρ | Meaning | Typical index emphasis |
|---|---------|------------------------|
| **Positive** | Nearby → similar vectors | Spatial-partitioned HNSW, spatial-first cascade shine |
| **≈ 0** | Independent | Filtered ANN (ACORN); vector-first needs **large** oversample |
| **Negative** | Nearby → dissimilar vectors | Boundary risk for partitions; **stitching**, **higher efSearch**, avoid naive vector-first |

Full analysis: [spatial_vector_correlation.md](spatial_vector_correlation.md). The **cost model** combines **σ_s** and **ρ̂**: [cost_model.md](cost_model.md).

## Candidate Approaches

| # | Approach | Type | Key Idea | Best For |
|---|----------|------|----------|----------|
| 1 | [Spatial-Partitioned HNSW](spatial_partitioned_hnsw.md) | Partition-based | S2 cell partitions with per-partition HNSW | Selective spatial queries |
| 2 | [R-tree + HNSW](r_tree_plus_hnsw.md) | Hierarchical | R-tree for spatial, HNSW at leaf nodes | Arbitrary spatial predicates |
| 3 | [Filtered HNSW](filtered_hnsw.md) | Graph-modification | ACORN-style traversal with S2 cell metadata | Moderate selectivity |
| 4 | [Hybrid Graph](hybrid_graph.md) | Unified structure | Single graph with spatial+vector edges | Uniform data distributions |
| 5 | [Cascade Pipeline](cascade_pipeline.md) | Query-level | Spatial filter → ANN pipeline with cost model | Simple implementation |

## Taxonomy

See [approach_taxonomy.md](approach_taxonomy.md) for a detailed classification.

## Recommendation (Preliminary)

Based on literature and industry analysis, the recommended approach is a **tiered design**:

1. **Tier 0 (Immediate)**: Cascade pipeline — spatial filter first, then existing HNSW on survivors. Requires minimal code changes, leverages existing infrastructure.

2. **Tier 1 (Near-term)**: Filtered HNSW with S2 cell metadata — ACORN-style traversal where spatial predicates are evaluated by S2 cell containment checks during HNSW graph traversal.

3. **Tier 2 (Medium-term)**: Spatial-partitioned HNSW — partition data by S2 cells within each segment, build per-partition HNSW graphs. The most performant approach for selective spatial queries.

This tiered approach provides incremental value while building toward the full hybrid index.

**Correlation note:** Tier 2 (spatial-partitioned HNSW) is **ideal when ρ̂ ≥ 0**; if **ρ̂ ≪ 0**, invest in **cross-partition edges** or prefer **Tier 1** filtered HNSW until stitching lands.

## Cost Model

See [cost_model.md](cost_model.md) for the selectivity-based strategy selection model and **correlation-aware** adjustments.

## Evaluation

See [benchmarks/](benchmarks/) for workload definitions and expected baselines.
