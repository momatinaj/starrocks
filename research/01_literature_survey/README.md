# Literature Survey

## Scope

This survey covers academic work relevant to hybrid GIS+Vector indexing, organized into four areas:

1. **Spatial Indexes** — classical and modern spatial data structures
2. **Vector Indexes** — ANN algorithms and data structures
3. **Hybrid Approaches** — papers that combine spatial and vector search
4. **Filtered ANN** — general predicate-aware ANN search (superset of spatial filtering)

## Methodology

- Venue focus: VLDB, SIGMOD, ICDE, WWW, NeurIPS, ICML, KDD
- Time range: 2020-2025 (with classical foundations from earlier)
- Search terms: "spatial vector index", "filtered ANN", "constrained nearest neighbor", "geo-tagged vector search", "predicate-aware HNSW", "range-filtered ANNS"
- Sources: Google Scholar, Semantic Scholar, arXiv, ACM DL, DBLP

## Key Findings Summary

| Area | Key Papers | Main Takeaway |
|------|-----------|---------------|
| Filtered ANN | ACORN, Filtered-DiskANN, Compass, NaviX | ACORN's predicate subgraph traversal is SOTA; adopted by Lucene |
| Spatial ANN | Mesh, LIST, KHI | Mesh directly addresses geo-tagged vector search with workload-aware index |
| Range-Filtered ANN | UNIFY, Dynamic Segment Graph, WoW, IVF² | Active area; selectivity-adaptive strategies are critical |
| Classical Spatial | R-tree, R*-tree, S2, H3 | S2 cells provide hierarchical spatial partitioning already in StarRocks |

## Two Architectural Patterns

The literature reveals two dominant approaches:

1. **Graph-modification**: Modify HNSW/graph traversal to skip filtered-out nodes during search
   - Representatives: ACORN, Filtered-DiskANN, NaviX
   - Pro: No structural changes to index, predicate-agnostic
   - Con: Traversal efficiency degrades with very selective filters

2. **Partition-based**: Partition data spatially, build per-partition vector indexes
   - Representatives: Mesh, KHI, IVF², spatial-IVF
   - Pro: Naturally aligns with segment-based storage, good for very selective spatial queries
   - Con: Cross-partition queries are expensive, partition boundaries cause recall loss

**Correlation:** Papers rarely distinguish **positive / zero / negative** correlation between **spatial proximity** and **embedding similarity**; see [hybrid_approaches.md](hybrid_approaches.md) and [spatial_vector_correlation.md](../03_algorithmic_design/spatial_vector_correlation.md).

## Navigation

- [Spatial Indexes](spatial_indexes.md)
- [Vector Indexes](vector_indexes.md)
- [Hybrid Approaches](hybrid_approaches.md)
- [Filtered ANN](filtered_ann.md)
- [Paper Notes](papers/paper_notes/)
- [Bibliography](papers/bibliography.bib)
