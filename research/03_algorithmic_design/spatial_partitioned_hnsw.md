# Spatial-Partitioned HNSW

## Core Idea

Partition the vector data within each StarRocks segment by S2 cell ID, then build an independent HNSW graph per partition. At query time, identify which S2 cells overlap the spatial query region, search only those partitions' HNSW graphs, and merge results.

## Architecture

```
Segment File (.vi for hybrid index)
├── S2 Cell Index (header)
│   ├── Cell ID → Partition offset
│   ├── Cell level metadata
│   └── Cell statistics (count, bbox)
│
├── Partition 0 (S2 Cell 3/04122...)
│   ├── HNSW Graph (M=16, efConstruction=200)
│   ├── Vector data (raw or quantized)
│   └── Row ID mapping
│
├── Partition 1 (S2 Cell 3/04123...)
│   ├── HNSW Graph
│   ├── Vector data
│   └── Row ID mapping
│
├── ...
│
└── Cross-Partition Edge Index (optional)
    └── Stitching edges between adjacent partitions
```

## Algorithm

### Index Build

```
Input: segment data with (row_id, location, embedding) tuples
Parameters: S2 level L, HNSW params (M, efConstruction)

1. For each row, compute S2 cell ID at level L from location
2. Group rows by S2 cell ID → partitions
3. For each partition with ≥ threshold rows:
   a. Build HNSW graph on partition's embeddings
   b. Record row ID mapping (HNSW node ID → segment row ID)
4. For small partitions (< threshold rows):
   a. Store vectors without HNSW (brute-force at query time)
5. (Optional) Stitch partitions:
   a. For each pair of adjacent S2 cells, add cross-partition edges
      between their boundary vectors
6. Write S2 Cell Index header with cell → partition mapping
```

### Query

```
Input: spatial region R, query vector q, k
Parameters: efSearch

1. Compute S2 cell covering of region R at level L
   → Set of candidate cells C
2. Look up partitions for cells in C using S2 Cell Index
3. For each matching partition P_i:
   a. If P_i has HNSW: search HNSW(q, k_i, efSearch) where k_i ≥ k
   b. If P_i has brute-force: linear scan on partition vectors
   c. For each result, verify exact spatial containment (point in R)
4. Merge results from all partitions, return top-k by distance
```

## S2 Level Selection

| S2 Level | Approx. Cell Area | Cells on Earth | Use Case |
|----------|-------------------|----------------|----------|
| 10 | ~80 km² | ~6M | Country-scale queries |
| 12 | ~5 km² | ~97M | City-scale queries |
| 14 | ~0.3 km² | ~1.5B | Neighborhood-scale |
| 16 | ~0.02 km² | ~25B | Block-scale |

**Recommended**: Level 12-14 for most use cases. Level 12 gives ~5 km² cells, which provides good spatial locality while keeping partition count manageable within a segment.

**Adaptive approach**: Use multiple levels. The S2 Cell Index header stores the level, and different segments can use different levels based on data density.

## Strengths

- Natural alignment with StarRocks' segment-based storage
- Leverages existing S2 library in StarRocks
- Each partition's HNSW graph is smaller → faster build, better cache behavior
- Selective spatial queries only search a few partitions
- Easy parallelization: search partitions concurrently

## Weaknesses

- **Boundary problem**: Vectors near cell boundaries may be close in vector space to vectors in adjacent cells but unreachable via HNSW traversal
- **Non-uniform partition sizes**: Dense urban areas may have huge partitions, sparse rural areas tiny ones
- **Cross-partition queries**: Queries spanning many cells degrade to searching many small HNSW graphs
- **Level selection**: Wrong S2 level causes either too many partitions (overhead) or too few (no spatial benefit)
- **Oversampling**: Each partition returns top-k candidates, but global top-k requires merging across partitions. Must request k' ≥ k per partition.

## Mitigation Strategies

### Boundary Problem
- **Cross-partition edges**: Stitch adjacent partition HNSW graphs at build time (as in StitchedVamana from Filtered-DiskANN)
- **Overlap**: Assign each vector to its primary cell AND adjacent cells (redundant storage, but ensures reachability)
- **Multi-level**: Build HNSW at a coarser S2 level as a fallback for boundary cases

### Non-Uniform Partitions
- **Adaptive level**: Use finer S2 level in dense areas, coarser in sparse areas
- **Minimum partition size**: Merge very small partitions into parent S2 cell
- **Maximum partition size**: Split very large partitions into child S2 cells

### Oversampling
- **Selectivity estimation**: Estimate how many results each partition will contribute, allocate k' proportionally
- **Progressive search**: Start with k' = k per partition, expand if merged results < k

## Complexity Analysis

| Operation | Complexity | Notes |
|-----------|-----------|-------|
| Build | O(n · log(n/P) · M) | n=total vectors, P=partitions, M=HNSW max connections |
| Query (spatial) | O(C · k · log(n/P)) | C=cells matching spatial query |
| Query (full scan) | O(P · k · log(n/P)) | Degrades when spatial predicate is non-selective |
| Memory | O(n · M) + O(P · overhead) | Same as HNSW plus partition metadata |

## Related Work

- **Mesh** (VLDB Journal 2025): Workload-aware spatial-range-constrained ANN with similar partitioning concept but optimized construction
- **KHI**: HNSW with attribute-space partitioning trees — generalizes this approach to arbitrary attributes
- **StitchedVamana** (Filtered-DiskANN): Cross-partition graph stitching technique directly applicable here
