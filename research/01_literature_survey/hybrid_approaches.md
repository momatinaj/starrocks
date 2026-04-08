# Hybrid Spatial+Vector Approaches

## Overview

This document surveys papers that directly address the combination of spatial and vector search. These are the most relevant works for our hybrid index design.

## Key Papers

### Mesh: Memory-Efficient Spatial-Range-Constrained ANN

**Paper**: Song et al., "Efficient top-k spatial-range-constrained approximate nearest neighbor search on geo-tagged high-dimensional vectors"  
**Venue**: The VLDB Journal 34(14) (Jan 2025)

**Problem**: k-RANNS — find top-k approximate nearest neighbors among geo-tagged vectors within a spatial range

**Approach**:
- Proposes a workload-aware index to optimize query efficiency for varying selective spatial ranges
- Solves the NP-hard combinatorial optimization problem of grouping data based on memory efficiency and query selectivity with a theoretically-guaranteed approximation algorithm
- Utilizes adaptive query algorithms based on estimated selectivity

**Relevance to StarRocks**: Directly addresses the memory constraints of production spatial-vector queries. The workload-aware and selectivity-adaptive methodologies map well to query optimization inside StarRocks.

---

### LIST: Learning to Index Spatio-Textual Data

**Paper**: Yin et al., "LIST: learning to index spatio-textual data for embedding based spatial keyword queries"  
**Venue**: The VLDB Journal 34(33) (April 2025) / arXiv:2403.07331

**Problem**: Embedding-based Top-k spatial keyword queries evaluating both text embeddings and spatial relevance.

**Approach**:
- A machine learning-based Approximate Nearest Neighbor Search index
- Learning-to-cluster technique groups spatially and textually relevant objects together
- Novel pseudo-label generation technique to handle the lack of labeled ground truth
- Integrates learning-based spatial relevance models with text relevance

**Relevance to StarRocks**: Demonstrates that data can be co-clustered along both spatial and embedding dimensions efficiently (3 orders of magnitude faster than baselines), which could inform segment-level partition schemes.

---

### KHI: HNSW with Attribute-Space Partitioning

**Paper**: Yu et al., "Efficient Approximate Nearest Neighbor Search under Multi-Attribute Range Filter"  
**Venue**: PVLDB 18(X) (2025/2024)

**Problem**: Multi-attribute numeric range constraints (RFANNS) combined with high-dimensional vector search.

**Approach**:
- Combines an attribute-space partitioning tree (KD-tree-like) with HNSW graphs attached to the tree nodes
- Employs a skew-aware splitting rule to bound the tree height and ensure balanced partitions
- Evaluates queries by matching partitions and aggregating corresponding HNSW sub-graphs

**Relevance to StarRocks**: Spatial attributes (Lat/Lon) naturally fit into multi-attribute KD-tree or spatial tree partitioning. The dual tree-plus-graph design supports structured range queries efficiently.

---

### IVF²: Fusing Classic and Spatial Inverted Indices

**Paper**: Landrum et al., "IVF² Index: Fusing Classic and Spatial Inverted Indices for Fast Filtered ANNS"  
**Venue**: NeurIPS '23 BigANN Filter Track Winner / ICML 2025 VecDB Workshop

**Problem**: Fast filtered ANNS for complex metadata combinations (e.g. AND predicates on boolean labels / metadata).

**Approach**:
- Integrates classical inverted file indices (for filtering labels) with spatial inverted indices (IVF for vectors)
- Uses memory-efficient bitvector representations for metadata
- Drastically reduces candidate search space before vector distances are computed, vastly outperforming baseline and standard IVF approaches for high-selectivity AND queries

**Relevance to StarRocks**: This is an excellent model for integrating bitmapped or inverted spatial indices with vector IVF indexing, particularly suited for StarRocks' existing inverted index infrastructure.

---

## Taxonomy of Hybrid Approaches

```
Hybrid Spatial+Vector Approaches
├── Index-Level Fusion
│   ├── Graph Modification (ACORN, Filtered-DiskANN)
│   │   └── Modify traversal to respect spatial predicates
│   ├── Dual-Index Fusion (IVF²)
│   │   └── Maintain separate structured and vector IVF structures, fuse at query time
│   └── Unified Structure (KHI, Mesh)
│       └── Single index partitioning space and vector attributes jointly
│
├── Query-Level Fusion
│   ├── Cascade Pipeline
│   │   └── Spatial filter first, then ANN on survivors
│   ├── Reverse Cascade
│   │   └── ANN first (oversampled), then spatial filter
│   └── Cooperative Execution (Compass)
│       └── Interleaved spatial (B+-tree) and vector index candidate generation
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

Many papers assume a **joint** structure (geo-tagged vectors, workload-aware **Mesh** construction) but rarely explicitly parameterize **positive / zero / negative** correlation between **spatial proximity** and **embedding similarity**:

- **LIST** and **KHI** target **learned** or **attribute-space** structure — often aligned with **positive** or **clustered** joint behavior.
- **Filtered ANN** (ACORN, Filtered-DiskANN) fits **ρ ≈ 0**: predicates are **generic**; the vector graph does **not** assume spatial locality.
- **Negative ρ** (diverse neighbors in dense areas) stresses **partition boundaries** and motivates **stitching** — relatively **underexplored** vs. generic filtered ANN.

See [spatial_vector_correlation.md](../../03_algorithmic_design/spatial_vector_correlation.md) for index recommendations by scenario.

## Research Gaps

1. **No work specifically addresses columnar OLAP + hybrid spatial-vector indexing.** All papers target OLTP/point-lookup workloads or specialized vector databases.

2. **Segment-based (LSM-tree style) hybrid indexes are underexplored.** StarRocks' segment architecture is different from the row-store or specialized vector DB architectures in the literature.

3. **Cost models for strategy selection** are largely ad-hoc. Few papers provide a principled cost model that considers spatial selectivity, **spatial–vector correlation**, vector index characteristics, and data distribution jointly.

## Open Questions

- Can ACORN's predicate-subgraph traversal be specialized for spatial predicates (using S2 cell containment) to outperform the general approach?
- What is the optimal granularity for spatial partitioning within a single segment?
- How to handle the "boundary problem" — vectors near partition boundaries that should be reachable from adjacent partitions?
