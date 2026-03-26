# R-tree + HNSW Cascade

## Core Idea

Build an R-tree over the spatial coordinates. At each leaf node, maintain a small HNSW graph over the vectors belonging to that spatial region. Query traverses the R-tree to find spatially relevant leaves, then searches their HNSW graphs.

## Architecture

```
R-tree (spatial structure)
├── Internal Node (MBR: [lat0-lat1, lon0-lon1])
│   ├── Internal Node (MBR: ...)
│   │   ├── Leaf Node (MBR: ...)
│   │   │   └── HNSW Graph (vectors in this spatial region)
│   │   ├── Leaf Node (MBR: ...)
│   │   │   └── HNSW Graph
│   │   └── ...
│   └── ...
└── ...
```

## Algorithm

### Index Build

```
1. Collect all (location, embedding, row_id) tuples from segment
2. Bulk-load R-tree on locations (Hilbert or STR packing)
3. For each leaf node:
   a. Collect embeddings of points in this leaf
   b. If count ≥ threshold: build HNSW graph
   c. Else: store vectors for brute-force search
4. Store R-tree structure + per-leaf HNSW indexes
```

### Query

```
Input: spatial region R, query vector q, k

1. Traverse R-tree: find all leaf nodes whose MBR intersects R
2. For each matching leaf:
   a. Search leaf's HNSW for top-k candidates
   b. Verify each candidate's exact location is within R
3. Merge results across all leaves, return global top-k
```

## Strengths

- **Arbitrary spatial predicates**: R-tree natively supports point, range, polygon, and distance queries
- **Data-adaptive**: R-tree splits based on actual data distribution, not fixed grid
- **Hierarchical pruning**: Non-matching subtrees are skipped entirely
- **Well-understood**: R-tree is a mature, well-studied data structure

## Weaknesses

- **Complex implementation**: R-tree + HNSW is a compound structure with complex build and maintenance
- **Memory overhead**: R-tree internal nodes + multiple HNSW graphs
- **No existing R-tree in StarRocks storage**: Would need to build or integrate one (S2 is available but is cell-based, not R-tree)
- **Leaf size sensitivity**: Too many vectors per leaf → HNSW search is slow. Too few → graph quality degrades.
- **Update complexity**: R-tree rebalancing + HNSW maintenance is expensive
- **Not segment-friendly**: R-tree assumes a single, mutable tree; StarRocks segments are immutable once written

## Comparison with Spatial-Partitioned HNSW

| Aspect | R-tree + HNSW | Spatial-Partitioned HNSW (S2) |
|--------|--------------|------------------------------|
| Spatial flexibility | High (arbitrary MBRs) | Medium (fixed S2 cell grid) |
| Data adaptivity | High (splits by data) | Low (fixed grid) |
| Implementation complexity | High | Medium |
| StarRocks alignment | Low (no R-tree infra) | High (S2 already integrated) |
| Immutable segment fit | Poor (R-tree assumes mutability) | Good (partitions are immutable) |
| Memory overhead | High (R-tree + multiple HNSW) | Medium (cell index + multiple HNSW) |

## Verdict

While R-tree + HNSW is theoretically attractive for its spatial flexibility, the implementation complexity and poor alignment with StarRocks' segment-based storage make it less practical than S2-based spatial partitioning. Consider this approach only if S2 cell-based partitioning proves insufficient for query accuracy.

The key advantage of R-tree (data-adaptive partitioning) can be partially replicated by using adaptive S2 levels based on data density, which is simpler to implement within StarRocks' existing infrastructure.
