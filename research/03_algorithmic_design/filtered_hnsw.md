# Filtered HNSW with Spatial Predicates

## Core Idea

Augment each HNSW node with spatial metadata (S2 cell ID, coordinates) and modify the graph traversal to evaluate spatial predicates during search. Based on the ACORN algorithm, adapted for spatial-specific optimizations.

## Architecture

```
HNSW Graph (standard structure, augmented nodes)
├── Layer L (top, sparse)
│   └── Nodes: { vector, s2_cell_id, lat, lon, connections[] }
├── Layer L-1
│   └── Nodes: { vector, s2_cell_id, lat, lon, connections[] }
├── ...
└── Layer 0 (bottom, dense)
    └── Nodes: { vector, s2_cell_id, lat, lon, connections[] }

Spatial Metadata Index (side structure)
├── S2 Cell → Node ID bitmap (for selectivity estimation)
└── Cell statistics (count per cell for cost model)
```

## Algorithm

### Index Build (Standard HNSW + Metadata)

```
Input: (row_id, location, embedding) tuples
Parameters: M, efConstruction, S2 level L

1. Build standard HNSW on embeddings (using TenANN)
2. For each node i:
   a. Store s2_cell_id = S2CellId(location_i, level=L)
   b. Store lat_i, lon_i for exact spatial checks
3. Build S2 Cell → Node bitmap for selectivity estimation
```

### Query (ACORN-style with Spatial Optimization)

```
Input: spatial region R, query vector q, k
Parameters: efSearch

1. Compute S2 cell covering of R at level L → candidate cells C
2. Estimate selectivity: σ = |nodes in C| / |total nodes|
3. Select strategy based on σ:

   If σ < 0.01:  (Very selective — tiny region)
     → Brute-force: scan all nodes in matching cells, compute exact distances
   
   If 0.01 ≤ σ < 0.08:  (Selective)
     → Blind 2-hop ACORN: at each HNSW step, if neighbor not in C,
       explore ALL of neighbor's neighbors (maxConn² candidates per hop)
   
   If 0.08 ≤ σ < 0.40:  (Moderate)
     → Directed 2-hop ACORN: if neighbor not in C, explore only
       neighbor's neighbors that are closer to q in vector space
   
   If 0.40 ≤ σ < 0.90:  (Permissive)
     → Standard HNSW with post-filter: search normally, discard
       results not in R
   
   If σ ≥ 0.90:  (Nearly all pass)
     → Standard HNSW: ignore spatial filter during search,
       apply as post-filter

4. For all strategies, verify exact spatial containment for final results
```

### Spatial-Specific Optimization: S2 Cell Short-Circuit

Standard ACORN evaluates predicates per node. For spatial predicates, we can optimize:

```
// Instead of per-point containment check:
bool passes_filter(node) {
    return region.Contains(S2Point(node.lat, node.lon));  // Expensive
}

// Use S2 cell hierarchy for fast pre-check:
bool passes_filter_fast(node) {
    // Level 1: Cell fully inside region? → pass without point check
    if (region.MayIntersect(S2Cell(node.s2_cell_id))) {
        if (region.Contains(S2Cell(node.s2_cell_id))) {
            return true;  // Entire cell inside region — all points pass
        }
        // Level 2: Cell partially overlaps — need exact point check
        return region.Contains(S2Point(node.lat, node.lon));
    }
    return false;  // Cell doesn't intersect region — skip
}
```

This three-level check (cell outside / cell inside / point check) is significantly faster than per-point containment for large regions covering many cells.

## Memory Overhead

Per HNSW node:
- S2 cell ID: 8 bytes (uint64)
- Latitude: 4 bytes (float32)
- Longitude: 4 bytes (float32)
- Total overhead: 16 bytes per node

For a segment with 1M vectors: 16 MB additional memory. Acceptable.

## Strengths

- **Predicate-agnostic**: Works with any spatial predicate expressible via S2 cells
- **No structural change to HNSW**: Standard HNSW build, augmented traversal
- **Leverages existing TenANN**: Modify traversal, not construction
- **Selectivity-adaptive**: Strategy selection based on spatial selectivity
- **S2 short-circuit**: Spatial-specific optimization that outperforms generic ACORN for spatial predicates

## Weaknesses

- **Graph connectivity**: HNSW edges don't consider spatial proximity — selective spatial filters may disconnect the subgraph
- **2-hop overhead**: Blind 2-hop exploration is expensive for very selective filters
- **Not spatially partitioned**: All nodes in a single graph — cache behavior for spatial queries is poor
- **No spatial locality in graph structure**: Spatially close points may be far apart in the HNSW graph

## Comparison with ACORN

| Aspect | Generic ACORN | Our Spatial-Optimized ACORN |
|--------|--------------|---------------------------|
| Predicate evaluation | Per-node, generic | S2 cell short-circuit (3-level) |
| Selectivity estimation | General statistics | S2 cell coverage count |
| Threshold tuning | Fixed (8%, 40%) | Can be spatial-density-aware |
| Additional metadata | None (predicate column exists) | +16 bytes/node (S2 cell ID + coords) |

## When to Use

This approach is best when:
- The spatial query region varies widely (some queries cover small areas, others large areas)
- The selectivity is typically moderate (10-50% of data in region)
- Implementation simplicity is prioritized over peak performance
- The existing HNSW infrastructure should be minimally modified

## When NOT to Use

- Very selective spatial queries (<1% of data) → spatial-partitioned HNSW is better
- Very permissive spatial queries (>90% of data) → standard HNSW with post-filter is sufficient
- Data has strong spatial clustering → spatial-partitioned HNSW exploits this structure
