# Filtered / Constrained ANN

## Overview

Filtered ANN (or constrained ANN) is the general problem of finding approximate nearest neighbors that satisfy arbitrary predicates. Spatial filtering is a special case where the predicate is a geometric containment or distance test. This document surveys the general filtered ANN literature, which provides techniques applicable to spatial filtering.

## Key Papers

### ACORN: Performant and Predicate-Agnostic Search

**Paper**: Qin et al., "ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data"  
**Venue**: arXiv:2403.04871 (2024)  
**Code**: Adopted by Apache Lucene 10.2.0

**Problem**: Filtered ANN where predicates are not known at index build time and can be arbitrary.

**Key Idea — Predicate Subgraph Traversal**:
- Standard HNSW search gets "stuck" when many neighbors fail the predicate, requiring expensive backtracking
- ACORN expands exploration to neighbor-of-neighbors (2-hop traversal) to bypass filtered-out nodes
- This emulates an ideal "predicate subgraph" — the HNSW graph restricted to nodes passing the filter

**Algorithm**:
1. Start greedy search from entry point (standard HNSW)
2. At each step, if a neighbor fails the predicate, explore that neighbor's neighbors (2-hop)
3. Only score/rank vectors that pass the predicate
4. Adaptive threshold: if filter passes >60% of data, fall back to standard HNSW (overhead not worth it)

**Relevance to StarRocks**:
- Predicate-agnostic means spatial predicates work without modification
- Can be implemented within TenANN's HNSW traversal
- The selectivity-adaptive approach maps to a cost model in the query planner

---

### Filtered-DiskANN

**Paper**: Gollapudi et al., "Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters"  
**Venue**: ACM Web Conference (WWW), 2023  

**Problem**: ANN search with label/attribute filters, optimized for SSD-based search.

**Key Idea — Filter-Aware Graph Construction**:
- Standard graph indexes build edges based only on vector similarity
- Filtered-DiskANN modifies the prune procedure to consider label metadata during construction
- Ensures that the graph has good connectivity within each label class

**Two Algorithms**:
1. **StitchedVamana**: Build separate graphs per label, then stitch them together
2. **FilteredVamana**: Build a single graph with filter-aware pruning

**Relevance to StarRocks**:
- If S2 cell IDs are treated as labels, the filter-aware construction ensures graph connectivity within spatial regions
- StitchedVamana maps well to a segment-based architecture: build per-spatial-partition graphs, stitch across partitions

---

### Compass: General Filtered Search across Vector and Structured Data

**Paper**: Ye et al., "Compass: General Filtered Search across Vector and Structured Data"  
**Venue**: arXiv:2510.27141 (Nov 2025)

**Problem**: General filtered search combining high-dimensional vector search with complex relational filtering without new index designs.

**Key Idea — Cooperative Query Execution**:
- Leverages established index structures: HNSW and IVF for vectors, B+-trees for structured attributes
- Uses a shared candidate queue to coordinate candidate generation and predicate evaluation across modalities
- Maintains generality by allowing arbitrary conjunctions, disjunctions, and range predicates

**Relevance to StarRocks**:
- Maps naturally to StarRocks' query engine by combining existing vector index scan with structured (spatial) index scan
- Bypasses the need for a completely new unified index structure, making it a highly practical approach

---

### NaviX: A Native Vector Index Design for Graph DBMSs

**Paper**: Sehgal & Salihoglu, "NaviX: A Native Vector Index Design for Graph DBMSs With Robust Predicate-Agnostic Search Performance"  
**Venue**: PVLDB 18(11): 4438-4450 (2025)

**Problem**: Disk-based vector index leveraging the core storage and query-processing of the underlying GDBMS, supporting predicate-agnostic filtered search.

**Key Idea**: 
- Native integration in Kuzu GDBMS built on HNSW.
- Prefiltering approach that passes the selection subset to the kNN operator.
- Proposes an "adaptive-local" heuristic using the local selectivity of each vector in the HNSW graph to adjust search behavior iteratively.

**Relevance**: Validates deep DBMS integration and the importance of adapting search strategies dynamically based on local selectivity.

---

### UNIFY: Unified Index for Range Filtered Approximate Nearest Neighbors Search

**Paper**: Liang et al., "UNIFY: Unified Index for Range Filtered Approximate Nearest Neighbors Search"  
**Venue**: PVLDB 18(4): 1118-1130 (2024)

**Problem**: Range Filtered Approximate Nearest Neighbors Search (RF-ANNS) over high-dimensional vectors with continuous attributes.

**Key Ideas**:
- Introduces Segmented Inclusive Graph (SIG) which segments the dataset by attribute values
- Hierarchical Segmented Inclusive Graph (HSIG) enables efficient hybrid filtering with logarithmic complexity and incremental data insertion
- Supports pre-, post-, and hybrid filtering seamlessly within a single index structure

**Relevance**: The segmentation of graphs based on ranges directly parallels partitioning HNSW graphs by S2 spatial cell ranges.

---

### Dynamic Segment Graph

**Paper**: Peng et al., "Dynamic Range-Filtering Approximate Nearest Neighbor Search"  
**Venue**: PVLDB 18(10): 3256-3268 (2025)

**Problem**: Dynamic range-filtered ANN where vectors arrive in arbitrary order, requiring efficient insertions.

**Key Ideas**:
- Compresses a set of HNSW graphs into a "dynamic segment graph" with lossless capabilities under certain conditions
- Substantially reduces the index size increment (O(log n) expected new edges per insertion) while preserving query performance

**Relevance**: Crucial for StarRocks' data ingestion pipeline to support dynamic insertions without full index rebuilds.

---

### WoW: A Window-to-Window Incremental Index

**Paper**: Wang et al., "WoW: A Window-to-Window Incremental Index for Range-Filtering Approximate Nearest Neighbor Search"  
**Venue**: SIGMOD 2026 / arXiv:2508.18617 (Aug 2025)

**Problem**: RFANNS index that needs to be constructed incrementally and handle arbitrary range filters.

**Key Ideas**:
- Hierarchical window graphs with varying window sizes managed by a Weighted Balanced Tree (WBT)
- Fully incremental, unordered insertions without data layout reorganization in $O(\log^2 n)$
- 4x faster query speeds than prior incremental indexes

**Relevance**: highly relevant for continuous data loading scenarios (like StarRocks Stream Load) where new data batches must be merged dynamically.

## Strategy Selection by Selectivity

A critical finding across all papers is that the optimal strategy depends on **filter selectivity** (fraction of data passing the filter):

| Selectivity | Best Strategy | Reasoning |
|-------------|--------------|-----------|
| Very Low (<1%) | Brute-force on filtered set / Pre-filtering | Too few candidates for graph traversal to help |
| Low (1-10%) | Pre-filter + brute-force or small HNSW | Graph connectivity too degraded for efficient search |
| Medium (10-50%) | ACORN-style 2-hop or Compass cooperative execution | Best trade-off between filtering and graph utilization |
| High (>50%) | Standard HNSW with post-filtering | Most nodes pass filter; overhead of filtering during traversal not worthwhile |

## Open Questions

- Can selectivity estimation be done efficiently using S2 cell statistics?
- How to handle the transition between strategies smoothly (avoid performance cliffs)?
- What is the overhead of adding spatial metadata to HNSW nodes for predicate evaluation during traversal?
