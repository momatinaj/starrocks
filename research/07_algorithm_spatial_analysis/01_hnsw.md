# HNSW — Spatial Query Suitability

## 1. Algorithm Summary

**Hierarchical Navigable Small World (HNSW)** (Malkov & Yashunin, *IEEE Transactions on Pattern Analysis and Machine Intelligence*, 2018) builds a multi-layer navigable small-world graph: upper layers are sparse and provide long-range hops; the bottom layer is dense and refines nearest neighbors. Search is greedy layer-by-layer with a beam controlled by `efSearch`; construction uses `M` (max neighbors) and `efConstruction`. See [vector_indexes.md](../01_literature_survey/vector_indexes.md) and [bibliography.bib](../01_literature_survey/papers/bibliography.bib) (`hnsw2018`).

## 2. Mechanism Deep-Dive

**Build**

1. Insert points one by one (or batch variants). For each new point, find entry points from the top layer down.
2. At each layer, connect the new point to up to `M` neighbors chosen to preserve small-world navigability (closer points preferred in lower layers).
3. Prune excess edges per layer to cap degree and preserve the “navigable” property.

**Query**

1. Start from an entry point (often highest layer).
2. Greedy descent: at each layer, move to neighbors that minimize distance to the query until no improvement (local minimum), then drop to the next layer.
3. On layer 0, maintain a dynamic list of candidates (beam width `efSearch`) and expand neighbors until convergence; return approximate k-NN.

HNSW optimizes **vector similarity only**; edges do not encode geography, attributes, or predicates.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|--------------|
| **`ST_Contains(polygon, point)`** | Treat as a **boolean filter** on each candidate. Pure HNSW does not prune the search by geometry; you either post-filter k-ANN results (may miss true k if many points outside the polygon are closer in vector space) or combine with a spatial index / cell metadata and modified traversal (see [filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)). |
| **`ST_DWithin(point, point, r)`** | Same pattern: metric in geographic space vs. vector space. **Circle / buffer** tests reduce to distance checks; still a filter unless the index is distance-aware. Highly selective DWithin favors **prefilter** or **partitioned** strategies over naive HNSW-only. |
| **`ST_Intersects(geometry, geometry)`** | General polygons/lines increase predicate cost and irregular “valid” sets in the embedding graph; HNSW has **no** structural support. Feasible as **filter-after-ANN** or with **cell covering + filtered traversal** (ACORN-style), not from the base graph alone. |
| **Lat/lon range predicates** | Axis-aligned boxes map cleanly to **range filters** on stored coordinates. HNSW does not use lat/lon for routing; effectiveness depends on **selectivity** and whether you **pre**-restrict candidates (e.g., segment lists, S2 cells) vs. post-filter. |

Cross-cutting issue from [vector_indexes.md](../01_literature_survey/vector_indexes.md): when most nodes fail the spatial predicate, **graph connectivity among survivors degrades**, so greedy search can stall without extensions (2-hop, partition-aware graphs, or higher `efSearch`).

## 4. Evidence

- **Design goal**: Unstructured high-dim ANN; no claims about spatial predicates ([vector_indexes.md](../01_literature_survey/vector_indexes.md)).
- **Reported behavior (project synthesis)**: Good recall/latency tradeoffs for k-NN; **memory-resident** graph with **random access** patterns — less SSD-friendly than DiskANN-style single-layer graphs.
- **Hybrid literature**: [hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md) classifies plain graph modification approaches as **index-level** fusion where traversal respects predicates — **base HNSW is the prerequisite**, not the spatial solution by itself.

## 5. Proposed Spatial Optimizations

1. **Metadata per node**: Store `lat`, `lon`, and/or S2 cell id on each node for cheap predicate tests during traversal ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)).
2. **S2 short-circuit**: Use cell–region relationships (disjoint / fully inside / partial) before exact `ST_Contains` ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)).
3. **Selectivity-driven strategy**: For very selective regions, prefer spatial-first candidate generation; for permissive regions, HNSW + post-filter ([approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md), [filtered_ann.md](../01_literature_survey/filtered_ann.md)).
4. **Correlation awareness**: If spatial–vector correlation ρ is near zero, rely on **filtered** or **prefilter** pipelines rather than assuming spatial locality in the graph ([hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md)).

## 6. Verdict

**CONDITIONALLY SUITABLE**

HNSW is **the right core ANN structure** for StarRocks when combined with spatial **metadata**, **planner strategy**, and/or **filtered-ANN traversal** (e.g., ACORN). As a **standalone** index for arbitrary `ST_*` predicates, it is **not** sufficient: it does not guarantee recall under strict spatial filters without oversampling, prefiltering, or graph extensions.

## 7. StarRocks Fit

- **Already present**: TenANN implements HNSW (`be/src/storage/index/vector/tenann/`) per [vector_indexes.md](../01_literature_survey/vector_indexes.md).
- **Segment model**: Per-segment graphs align with batch loads; spatial metadata per node is a small fixed overhead ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)).
- **Gap**: Production spatial+vector plans will need **optimizer cost rules** (selectivity + ρ̂) and possibly **traversal hooks** for predicate-aware expansion — matching the **Index-Level × Selectivity-Adaptive** row in [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md).
