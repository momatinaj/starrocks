# Approach Taxonomy

## Classification Framework

Hybrid spatial+vector indexing approaches can be classified along **four** dimensions (see also [spatial_vector_correlation.md](spatial_vector_correlation.md)):

### Dimension 1: Integration Level

```
Integration Level
├── Query-Level (loosest coupling)
│   ├── Sequential: spatial filter → ANN search
│   ├── Reverse: ANN search → spatial filter
│   └── Cooperative: interleaved index traversal (Compass)
│
├── Index-Level (moderate coupling)
│   ├── Metadata-augmented: add spatial info to vector index nodes
│   ├── Filter-aware construction: build graph considering spatial proximity
│   └── Dual-index fusion: separate indexes with coordinated query execution (IVF²)
│
└── Structure-Level (tightest coupling)
    ├── Unified graph: single graph encoding both spatial and vector proximity
    ├── Hierarchical: spatial tree with embedded vector indexes at leaves
    └── Spatial-partitioned: vector indexes per spatial partition
```

### Dimension 2: Spatial Representation

```
Spatial Representation
├── Cell-Based (discrete)
│   ├── S2 cells — hierarchical, equal-area, already in StarRocks
│   ├── H3 hexagons — uniform neighbors, analytics-friendly
│   └── Geohash — string-based, B-tree compatible
│
├── Geometry-Based (continuous)
│   ├── MBR (Minimum Bounding Rectangle) — R-tree nodes
│   ├── WKT/WKB geometries — exact shapes
│   └── S2 region coverings — S2 cell approximation of arbitrary shapes
│
└── Coordinate-Based (raw)
    ├── Lat/lon pairs — simple distance computation
    └── Projected coordinates — Cartesian approximation
```

### Dimension 3: Adaptivity

```
Adaptivity
├── Static
│   ├── Fixed strategy regardless of query
│   └── Fixed spatial granularity
│
├── Selectivity-Adaptive
│   ├── Choose strategy based on spatial filter selectivity
│   └── Adjust search parameters (efSearch, nprobe) based on expected survivors
│
└── Workload-Adaptive
    ├── Learn spatial distribution from query patterns (Mesh)
    ├── Adaptive spatial granularity based on data density
    └── Online strategy tuning based on runtime feedback
```

### Dimension 4: Spatial–Vector Correlation (ρ)

How strongly **geographic proximity** aligns with **embedding similarity** (estimated at dataset/segment level).

```
Spatial–Vector Correlation ρ
├── Positive (ρ ≫ 0)
│   ├── Nearby points tend to be similar in vector space
│   ├── Spatial partitions align with vector neighborhoods
│   └── Spatial-partitioned HNSW and spatial-first cascade are favored
│
├── Near-zero (ρ ≈ 0)
│   ├── Location and embeddings are effectively independent
│   ├── No “free” locality in the vector graph from space
│   └── Filtered ANN (ACORN), selectivity-driven plans; vector-first needs heavy oversampling
│
└── Negative (ρ ≪ 0)
    ├── Neighbors on the map often differ in embedding space
    ├── Partition boundaries may cut true vector nearest neighbors
    └── Prefer cross-partition stitching, higher efSearch / 2-hop; hybrid spatial edges; avoid naive vector-first
```

**Planner implication:** combine **σ_s** (selectivity) with **ρ̂** (estimated correlation) to choose **partitioned vs. filtered-only**, **whether to enable boundary stitching**, and **vector-first oversampling factor**.

## Cross-Cutting Concerns

### Build Time vs Query Time Flexibility

| Approach | Build-time decisions | Query-time flexibility |
|----------|---------------------|----------------------|
| Cascade Pipeline | None (uses existing indexes) | Full (any spatial predicate) |
| Filtered HNSW (ACORN) | None (standard HNSW) | Full (predicate-agnostic) |
| Filtered HNSW (Filtered-DiskANN) | Spatial labels determine graph structure | Limited to indexed spatial partitions |
| Spatial-Partitioned HNSW | Partition scheme is fixed | Limited to partition boundaries |
| R-tree + HNSW | R-tree structure fixed | Full (R-tree supports arbitrary queries) |
| Hybrid Graph | Graph structure encodes both | Moderate |

### Memory vs Query Performance Trade-off

| Approach | Memory Overhead | Query Performance |
|----------|----------------|-------------------|
| Cascade Pipeline | None (reuses existing) | Baseline |
| Filtered HNSW | +S2 cell ID per node (8 bytes) | 2-5x over cascade for medium selectivity |
| Spatial-Partitioned HNSW | +overhead per partition graph | Best for selective queries |
| R-tree + HNSW | +R-tree structure | Best for arbitrary spatial predicates |
| Hybrid Graph | +spatial edges (~2x edge count) | Competitive but complex |

### Incremental Update Complexity

| Approach | Insert | Delete | Compaction |
|----------|--------|--------|------------|
| Cascade Pipeline | Trivial | Trivial | Trivial |
| Filtered HNSW | Standard HNSW insert + metadata | Standard HNSW delete | Standard |
| Spatial-Partitioned HNSW | Route to partition, insert into partition HNSW | Remove from partition | Merge partitions |
| R-tree + HNSW | R-tree insert + HNSW insert at leaf | R-tree delete + HNSW delete | Complex |
| Hybrid Graph | Complex (maintain both edge types) | Complex | Complex |

## Recommended Classification for StarRocks

Based on StarRocks' constraints (segment-based storage, columnar engine, per-segment indexes):

**Primary**: Spatial-Partitioned HNSW (Structure-Level × Cell-Based × Selectivity-Adaptive)
- S2 cell partitioning within segments
- Per-partition HNSW graph
- Cost-model-driven strategy selection at query time
- **Best when ρ̂ is positive or unknown**; if ρ̂ is **negative**, pair with **cross-partition stitching** or fall back to **filtered HNSW**

**Fallback**: Filtered HNSW (Index-Level × Cell-Based × Selectivity-Adaptive)
- ACORN-style traversal with S2 cell ID metadata per node
- Predicate-agnostic — works with any spatial predicate expressible via S2 cells
- **Especially strong when ρ̂ ≈ 0** (independence); still **required baseline** when ρ is unknown

**Baseline**: Cascade Pipeline (Query-Level × Geometry-Based × Static)
- Spatial filter using existing ST_Contains, then existing HNSW on survivors
- No index changes required

**Correlation-aware refinement**: See [spatial_vector_correlation.md](spatial_vector_correlation.md) for **positive / zero / negative ρ** and how each approach ranks.
