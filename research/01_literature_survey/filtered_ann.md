# Filtered / Constrained ANN

## Overview

Filtered ANN (or constrained ANN) is the general problem of finding approximate nearest neighbors that satisfy arbitrary predicates. Spatial filtering is a special case where the predicate is a geometric containment or distance test. This document surveys the general filtered ANN literature, which provides techniques applicable to spatial filtering.

## Key Papers

### ACORN: Performant and Predicate-Agnostic Search

**Paper**: Qin et al., "ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data"  
**Venue**: arXiv 2403.04871, 2024  
**Code**: Adopted by Apache Lucene 10.2.0 (February 2025)

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

**Results**: 2-1000x higher throughput at fixed recall compared to prior methods.

**Lucene Implementation (ACORN-1)**:
- Selectivity-based heuristics: blind 2-hop for low selectivity (≤0.08), directed 2-hop for medium (≤0.4), standard 1-hop for high selectivity (>0.4)
- Up to 5x faster filtered kNN search with minimal recall degradation
- Integrated into Elasticsearch 8.x via Lucene 10.2.0

**Relevance to StarRocks**:
- Predicate-agnostic means spatial predicates work without modification
- Can be implemented within TenANN's HNSW traversal
- The selectivity-adaptive approach maps to a cost model in the query planner
- Already production-proven in Elasticsearch at scale

---

### Filtered-DiskANN

**Paper**: Gollapudi et al., "Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters"  
**Venue**: ACM Web Conference (WWW), 2023  
**Author**: Microsoft Research

**Problem**: ANN search with label/attribute filters, optimized for SSD-based search.

**Key Idea — Filter-Aware Graph Construction**:
- Standard graph indexes build edges based only on vector similarity
- Filtered-DiskANN modifies the prune procedure to consider label metadata during construction
- Ensures that the graph has good connectivity within each label class

**Two Algorithms**:
1. **StitchedVamana**: Build separate graphs per label, then stitch them together
2. **FilteredVamana**: Build a single graph with filter-aware pruning

**Results**: Order of magnitude more efficient than prior methods on real-world filtered queries.

**Relevance to StarRocks**:
- If S2 cell IDs are treated as labels, the filter-aware construction ensures graph connectivity within spatial regions
- StitchedVamana maps well to a segment-based architecture: build per-spatial-partition graphs, stitch across partitions
- Requires knowing the spatial partitioning at index build time (unlike ACORN which is predicate-agnostic)

---

### Compass: General Filtered Search

**Paper**: "Compass: General Filtered Search across Vector and Structured Data"  
**Venue**: arXiv 2510.27141, 2024

**Problem**: General filtered search combining vector similarity with arbitrary relational predicates.

**Key Idea — Cooperative Query Execution**:
- Does not build a new index structure
- Uses existing HNSW/IVF for vectors and B+-trees for attributes
- Coordinates candidate generation across both index types
- Supports arbitrary conjunctions, disjunctions, and range predicates

**Results**: Outperforms NaviX (the prior general framework); matches specialized single-attribute indices.

**Relevance to StarRocks**:
- The cooperative execution model maps naturally to StarRocks' query engine
- Could combine existing vector index scan with a new spatial index scan
- Does not require a new unified index structure — simpler to implement as a first step
- However, may not achieve the performance of a truly hybrid index

---

### NaviX: Native Vector Index for Graph DBMSs

**Paper**: Sehgal & Salihoglu, "NaviX: A Native Vector Index Design for Graph DBMSs With Robust Predicate-Agnostic Search Performance"  
**Venue**: VLDB 2025

**Problem**: Vector index deeply integrated with DBMS predicate evaluation.

**Key Idea**: Design the vector index as a first-class DBMS component that natively supports predicate evaluation during traversal, rather than treating filtering as an afterthought.

**Relevance**: Validates the approach of deep DBMS integration for filtered vector search (rather than bolt-on filtering).

---

### UNIFY: Unified Proximity Graph

**Paper**: Referenced in VLDB 2024 proceedings  
**Problem**: Range-filtered ANNS with pre-, post-, and hybrid filtering strategies.

**Key Ideas**:
- Segmented Inclusive Graph (SIG): partitions the graph by attribute ranges
- Hierarchical SIG: multi-level partitioning for efficient range queries
- Supports pre-filtering (search only matching partition), post-filtering (search all, filter results), and hybrid (adaptive combination)

**Relevance**: The SIG concept directly applies to spatial range queries — partition the HNSW graph by S2 cell levels, search relevant segments.

---

### Dynamic Segment Graph

**Paper**: VLDB 2025  
**Problem**: Dynamic range-filtered ANN where vectors arrive in arbitrary order.

**Key Ideas**:
- Compresses multiple HNSW graphs (one per range segment) into a single graph
- Maintains search performance with minimal index size increase
- Handles dynamic insertions without full rebuild

**Relevance**: Important for StarRocks' data ingestion pipeline — new data segments must be indexed without rebuilding the entire index.

---

### WoW: Window-to-Window Incremental Index

**Paper**: arXiv 2508.18617, 2025  
**Problem**: Range-filtering ANN with incremental index construction.

**Key Ideas**:
- Incremental construction: new windows build on previous windows' structure
- 4x faster queries than other incremental approaches
- Handles arbitrary range filters

**Relevance**: Relevant for StarRocks' compaction pipeline — when segments merge, the index should be incrementally updatable.

## Strategy Selection by Selectivity

A critical finding across all papers is that the optimal strategy depends on **filter selectivity** (fraction of data passing the filter):

| Selectivity | Best Strategy | Reasoning |
|-------------|--------------|-----------|
| Very Low (<1%) | Brute-force on filtered set | Too few candidates for graph traversal to help |
| Low (1-10%) | Pre-filter + brute-force or small HNSW | Graph connectivity too degraded for efficient search |
| Medium (10-50%) | ACORN-style 2-hop or partition-based search | Best trade-off between filtering and graph utilization |
| High (50-90%) | Standard HNSW with post-filtering | Most nodes pass filter; overhead of filtering during traversal not worthwhile |
| Very High (>90%) | Standard HNSW (ignore filter during search) | Nearly all nodes pass; apply filter as post-processing |

## Open Questions

- Can selectivity estimation be done efficiently using S2 cell statistics?
- How to handle the transition between strategies smoothly (avoid performance cliffs)?
- What is the overhead of adding spatial metadata to HNSW nodes for predicate evaluation during traversal?
