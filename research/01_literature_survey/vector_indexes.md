# Vector Indexes

## Overview

Vector indexes accelerate approximate nearest neighbor (ANN) search in high-dimensional spaces. For our hybrid index, they provide the similarity search component. The key question is which vector index structure best accommodates spatial filtering.

## Graph-Based Indexes

### HNSW (Hierarchical Navigable Small World)

**Paper**: Malkov & Yashunin, "Efficient and Robust Approximate Nearest Neighbor using Hierarchical Navigable Small World Graphs" (IEEE TPAMI, 2018)

- Multi-layer graph with logarithmically decreasing density
- Top layers: long-range connections for fast navigation
- Bottom layer: short-range connections for precision
- Greedy search from top layer down, expanding neighborhood at each layer
- Key parameters: `M` (max connections), `efConstruction` (build beam width), `efSearch` (search beam width)

**Strengths for hybrid index**:
- Graph traversal can be modified to skip filtered-out nodes (ACORN approach)
- Per-node metadata is easy to add (spatial coordinates, S2 cell ID)
- Good recall at reasonable memory cost

**Weaknesses for hybrid index**:
- Graph connectivity degrades when many nodes are filtered out
- Random access pattern — not cache-friendly for large indexes
- Rebuild required when data changes

**StarRocks status**: Implemented via TenANN (`be/src/storage/index/vector/tenann/`)

### NSW (Navigable Small World)

**Paper**: Malkov et al., 2014

- Single-layer predecessor to HNSW
- Same greedy search principle, but without hierarchy
- Simpler but slower for large datasets
- Historical interest; HNSW superseded it

### Vamana (DiskANN)

**Paper**: Subramanya et al., "DiskANN: Fast Accurate Billion-point Nearest Neighbor Search on a Single Node" (NeurIPS, 2019)

- Single-layer graph optimized for SSD-based search
- RobustPrune procedure ensures bounded degree and low diameter
- Can index billions of vectors on a single machine with SSD
- Filtered-DiskANN variant incorporates label metadata into graph construction

**Relevance**: Filtered-DiskANN demonstrates that graph construction can be filter-aware, which is directly applicable to spatial filtering.

## Inverted File Indexes

### IVF (Inverted File)

- Clusters vectors using k-means, builds inverted list per centroid
- Search: find nearest centroids, scan their inverted lists
- Key parameter: `nlist` (number of centroids), `nprobe` (centroids to search)
- Simple, disk-friendly, good for large-scale datasets

**For hybrid index**: Spatial partitioning naturally maps to IVF clusters — each spatial partition is an inverted list.

### IVFPQ (IVF + Product Quantization)

- Combines IVF with product quantization for compression
- Sub-vectors are quantized independently
- Enables approximate distance computation from compressed codes
- Memory-efficient but lower recall than HNSW

**StarRocks status**: Implemented via TenANN

### ScaNN (Scalable Nearest Neighbors)

**Paper**: Guo et al., Google, 2020

- Uses anisotropic vector quantization
- Learns quantization that preserves inner products (not just distances)
- Partitioning + asymmetric hashing + reranking pipeline
- State-of-the-art on ANN benchmarks for certain workloads

### SPANN (Space-Partition Tree and Navigable Small World)

**Paper**: Chen et al., Microsoft, 2021

- Hierarchical balanced clustering using SPTAG library
- Posting lists stored on SSD, centroids in memory
- Supports efficient updates and deletions
- Good for billion-scale datasets on single machines

## Quantization Methods

### Product Quantization (PQ)
- Splits vectors into sub-vectors, quantizes each with separate codebook
- Enables fast distance computation via lookup tables
- 4-64x compression ratio

### Optimized Product Quantization (OPQ)
- Learns rotation matrix to minimize quantization error
- Better recall than PQ at same compression ratio

### Binary Quantization
- Maps each dimension to a single bit
- 32x compression, very fast Hamming distance
- Low recall but excellent for initial filtering/reranking pipeline

## Relevance to Hybrid Index

| Index | Filtering Adaptability | Disk Friendliness | StarRocks Status |
|-------|----------------------|-------------------|------------------|
| HNSW | High (ACORN, Filtered-DiskANN) | Medium | Implemented |
| IVF/IVFPQ | High (spatial partitions = clusters) | High | Implemented |
| DiskANN/Vamana | High (Filtered-DiskANN) | Very High | Not implemented |
| ScaNN | Medium | Medium | Not implemented |

## Recommendation

**HNSW** is the primary candidate because:
1. Already implemented in StarRocks via TenANN
2. Rich literature on filtered variants (ACORN, Filtered-DiskANN)
3. Good recall characteristics for K-ANN queries
4. Graph structure allows per-node spatial metadata

**IVF** is a strong secondary candidate because:
1. Also implemented in StarRocks
2. Spatial partitioning maps naturally to inverted lists
3. Disk-friendly for large datasets

## Open Questions

- Can HNSW and IVF be combined in a hybrid structure? (IVF for coarse spatial partitioning, HNSW within each partition)
- What is the memory overhead of adding S2 cell metadata to HNSW nodes?
- How does graph connectivity degrade when spatial filters remove 90%+ of nodes?
