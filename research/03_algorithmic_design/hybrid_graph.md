# Hybrid Graph

## Core Idea

Build a single graph where each node has two types of edges: vector-similarity edges (standard HNSW connections) and spatial-proximity edges (connections to spatially nearby nodes). During traversal, both edge types are used, with spatial edges providing shortcuts to geographically relevant regions.

## Architecture

```
Hybrid Graph Node:
{
  id: int,
  vector: float[D],
  location: (lat, lon),
  s2_cell_id: uint64,
  vector_edges: [node_id, ...],    // Standard HNSW connections (M edges)
  spatial_edges: [node_id, ...],   // Spatial proximity connections (M_s edges)
}
```

## Algorithm

### Index Build

```
1. Build standard HNSW on embeddings → vector_edges
2. Build spatial proximity graph:
   a. For each node, find M_s spatially nearest neighbors
   b. Store as spatial_edges
3. Optionally merge: replace some vector_edges with spatial_edges
   to maintain total degree budget
```

### Query

```
Input: spatial region R, query vector q, k

1. Start from HNSW entry point
2. At each step, consider candidates from BOTH edge types:
   a. Follow vector_edges → find nodes closer to q in vector space
   b. Follow spatial_edges → find nodes closer to R in spatial space
3. Score candidates by vector distance to q
4. Filter by spatial containment in R
5. Return top-k
```

### Edge Selection Strategy

During traversal, dynamically weight edge types:
- Far from spatial region R → prefer spatial_edges (navigate toward region)
- Inside spatial region R → prefer vector_edges (find nearest vectors)
- Near boundary of R → use both

## Strengths

- **Single unified structure**: No separate spatial index needed
- **Natural navigation**: Spatial edges provide shortcuts to relevant geographic regions
- **Smooth transition**: Gradually shifts from spatial to vector search as traversal approaches the target region

## Weaknesses

- **Double the edges**: M + M_s edges per node → higher memory and slower traversal per hop
- **Complex construction**: Two graph constructions (HNSW + spatial KNN)
- **Unclear benefit**: It's not clear that spatial edges provide better navigation than ACORN's 2-hop strategy
- **No existing implementation**: Novel approach with no production or academic validation
- **Difficult to maintain**: Updates must maintain both edge sets consistently
- **Parameter tuning**: M_s, edge weighting strategy, transition heuristics are all new hyperparameters

## Theoretical Analysis

The graph's navigability depends on having edges that span different scales in both spatial and vector dimensions. The key question is whether adding spatial edges improves the graph's navigability for spatially-filtered queries compared to:

1. Standard HNSW with ACORN traversal (no spatial edges, handle filtering at query time)
2. Spatial-partitioned HNSW (separate graphs per spatial partition)

**Hypothesis**: Spatial edges provide the most benefit when:
- Data has low correlation between spatial and vector similarity
- Queries have moderate spatial selectivity (10-50%)
- The spatial region is compact and well-connected in the spatial graph

**Counter-hypothesis**: The added complexity and memory of maintaining spatial edges is not justified when:
- ACORN's 2-hop already reaches spatially relevant nodes efficiently
- Spatial partitioning provides cleaner separation

## Verdict

This approach is **speculative and high-risk**. It has no academic or industry precedent for spatial+vector search. The added complexity (double edges, parameter tuning, construction cost) may not be justified by performance gains over simpler approaches (Filtered HNSW, Spatial-Partitioned HNSW).

**Recommendation**: Explore only if Filtered HNSW and Spatial-Partitioned HNSW prove insufficient in benchmarks. The concept could be valuable for a future research paper, but is too risky for initial implementation.
