# Hybrid Spatial+Vector Approaches

## Overview

This document surveys papers that directly address the combination of spatial and vector search. These are the most relevant works for our hybrid index design.

## Key Papers

### Mesh: Memory-Efficient Spatial-Range-Constrained ANN

**Paper**: "Efficient top-k spatial-range-constrained approximate nearest neighbor search on geo-tagged high-dimensional vectors"  
**Venue**: The VLDB Journal, January 2025  
**Problem**: k-RANNS — find top-k approximate nearest neighbors among geo-tagged vectors within a spatial range

**Approach**:
- Workload-aware index construction that optimizes for expected query patterns
- Solves the combinatorial optimization problem of partitioning geo-tagged vectors into groups that satisfy both spatial locality and vector similarity
- Theoretically-guaranteed approximation algorithm for index construction
- Adaptive query algorithm that adjusts execution strategy based on query selectivity

**Key Contributions**:
- Formal definition of the k-RANNS problem
- Memory-efficient index design (critical for large-scale deployments)
- Selectivity-adaptive query execution

**Relevance to StarRocks**: Directly addresses our problem. The workload-aware construction could leverage StarRocks' query statistics. The adaptive query strategy maps to a cost-model in the query planner.

---

### LIST: Learning to Index Spatio-Textual Data

**Paper**: "LIST: Learning to Index Spatio-Textual Data for Embedding based Spatial Keyword Queries"  
**Venue**: The VLDB Journal, 2024  
**Problem**: Top-k KNN spatial keyword queries where objects have both location and text embeddings

**Approach**:
- Learning-to-cluster technique that groups spatially and textually relevant objects
- Pseudo-label generation for training without ground truth
- Learning-based spatial relevance model integrated with text relevance models
- Encodes query keywords and object descriptions in separate embeddings

**Key Contributions**:
- ML-based index construction for spatio-textual data
- Unified model that considers both spatial proximity and semantic similarity

**Relevance to StarRocks**: The learning-to-cluster idea could inform how we partition data within segments — cluster by both spatial location and vector similarity rather than just one dimension.

---

### KHI: HNSW with Attribute-Space Partitioning

**Paper**: Referenced in hybrid search literature (2024)  
**Problem**: Multi-attribute range filtering on high-dimensional vectors

**Approach**:
- Combines HNSW graphs with attribute-space partitioning trees
- Partitions the attribute space (which includes spatial coordinates) and builds HNSW sub-graphs per partition
- Achieves 16.22x throughput improvement over baselines

**Key Contributions**:
- Formal framework for combining attribute-space partitioning with graph-based ANN
- Shows that attribute-aware partitioning significantly improves filtered search

**Relevance to StarRocks**: Spatial coordinates are just two attributes in the attribute-space. The partitioning tree approach could use S2 cell hierarchy as the spatial attribute partitioning.

---

### IVF²: Fusing Classic and Spatial Inverted Indices

**Paper**: "IVF2 Index: Fusing Classic and Spatial Inverted Indices for Fast Filtered ANNS"  
**Venue**: 2025  
**Problem**: Filtered ANNS with binary metadata constraints, including spatial

**Approach**:
- Builds two inverted file structures: one classic (by vector clusters) and one spatial (by location)
- Fuses them during query execution to efficiently handle spatial+vector queries
- Binary metadata constraints (point is/isn't in region)

**Key Contributions**:
- Dual-index fusion strategy
- Efficient for binary spatial predicates (contains/not contains)

**Relevance to StarRocks**: The dual-IVF approach maps well to StarRocks where we could maintain both a vector index and a spatial index within the same segment, fusing results during query execution.

---

## Broader Hybrid Search

### Hybrid Search at Scale: Full-Text + Vectors + Geolocation

**Reference**: de Melo, 2026 (industry article)  
**System**: OpenSearch with Azure OpenAI embeddings and HNSW

**Approach**:
- Combines BM25 full-text search, k-NN vector search, and geospatial queries
- HNSW algorithm optimization for the vector component
- Achieved 92% recall vs 65% with full-text alone

**Relevance**: Validates the real-world need for multi-modal search combining geo+vector.

## Taxonomy of Hybrid Approaches

```
Hybrid Spatial+Vector Approaches
├── Index-Level Fusion
│   ├── Graph Modification (ACORN, Filtered-DiskANN)
│   │   └── Modify traversal to respect spatial predicates
│   ├── Dual-Index Fusion (IVF²)
│   │   └── Maintain separate spatial and vector structures, fuse at query time
│   └── Unified Structure (KHI, Mesh)
│       └── Single index encoding both spatial and vector information
│
├── Query-Level Fusion
│   ├── Cascade Pipeline
│   │   └── Spatial filter first, then ANN on survivors
│   ├── Reverse Cascade
│   │   └── ANN first (oversampled), then spatial filter
│   └── Cooperative Execution (Compass)
│       └── Interleaved spatial and vector index traversal
│
└── Data-Level Fusion
    ├── Spatial Partitioning + Per-Partition ANN
    │   └── S2/H3 cells with embedded HNSW per cell
    ├── Learned Clustering (LIST)
    │   └── ML-based joint spatial+vector clustering
    └── Spatial-Aware Data Layout
        └── Co-locate spatially similar data in storage segments
```

## Spatial–Vector Correlation

Many papers assume a **joint** structure (geo-tagged vectors, workload-aware **Mesh** construction) but rarely label **positive / zero / negative** correlation between **spatial proximity** and **embedding similarity**:

- **LIST** and **KHI** target **learned** or **attribute-space** structure — often aligned with **positive** or **clustered** joint behavior.
- **Filtered ANN** (ACORN, Filtered-DiskANN) fits **ρ ≈ 0**: predicates are **generic**; the vector graph does **not** assume spatial locality.
- **Negative ρ** (diverse neighbors in dense areas) stresses **partition boundaries** and motivates **stitching** — relatively **underexplored** vs. generic filtered ANN.

See [spatial_vector_correlation.md](../../03_algorithmic_design/spatial_vector_correlation.md) for index recommendations by scenario.

## Research Gaps

1. **No work specifically addresses columnar OLAP + hybrid spatial-vector indexing.** All papers target OLTP/point-lookup workloads or specialized vector databases.

2. **Segment-based (LSM-tree style) hybrid indexes are unexplored.** StarRocks' segment architecture is different from the row-store or specialized vector DB architectures in the literature.

3. **Cost models for strategy selection** are ad-hoc. No paper provides a principled cost model that considers spatial selectivity, **spatial–vector correlation**, vector index characteristics, and data distribution jointly.

4. **Incremental maintenance** of hybrid indexes (as data is ingested) is poorly addressed. Most papers focus on static datasets.

## Open Questions

- Can ACORN's predicate-subgraph traversal be specialized for spatial predicates (using S2 cell containment) to outperform the general approach?
- What is the optimal granularity for spatial partitioning within a single segment?
- How to handle the "boundary problem" — vectors near partition boundaries that should be reachable from adjacent partitions?
