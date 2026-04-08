# ACORN — Spatial Query Suitability

## 1. Algorithm Summary

**ACORN** (*ACORN: Performant and Predicate-Agnostic Search Over Vector Embeddings and Structured Data*, Qin et al., arXiv:2403.04871, 2024) targets **filtered ANN**: approximate nearest neighbors among points satisfying a predicate not known at build time. It keeps an **HNSW** backbone but augments search with **predicate-subgraph** exploration — when direct neighbors fail the filter, ACORN expands to **neighbor-of-neighbor (2-hop)** links to bypass “dead zones” caused by filtering. Apache Lucene 10.2.0 adopted related ideas. See [filtered_ann.md](../01_literature_survey/filtered_ann.md) and [bibliography.bib](../01_literature_survey/papers/bibliography.bib) (`acorn2024`).

## 2. Mechanism Deep-Dive

**Build**

- Standard **HNSW** on vectors (same as [01_hnsw.md](01_hnsw.md)); predicates are **not** baked into edge selection.

**Query**

1. Initialize search from the usual HNSW entry point with beam parameters analogous to `efSearch`.
2. When considering a neighbor, if it **fails** the predicate, ACORN does not only backtrack — it explores **2-hop** neighbors to approximate connectivity of the **induced subgraph** of points that pass the filter.
3. Only vectors passing the predicate are scored toward the final top-k.
4. **Adaptive mode**: if the filter passes a large fraction of data (e.g., **> ~60%** in the project summary), fall back to **standard HNSW** with post-filtering to avoid 2-hop overhead when it does not pay off ([filtered_ann.md](../01_literature_survey/filtered_ann.md)).

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|--------------|
| **`ST_Contains(polygon, point)`** | **Direct fit**: polygon test is a **predicate** on point location. ACORN is **predicate-agnostic**, so any implementation of `Contains` (including S2-backed tests) can gate expansion and scoring. **Caveat**: very selective polygons shrink the effective subgraph — 2-hop cost grows; use **S2 cell covering** for cheap rejects ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)). |
| **`ST_DWithin(..., r)`** | Predicate = distance ≤ r in geographic space. Same as above: **works as a filter**; for large r (high selectivity), prefer adaptive fallback to vanilla HNSW + filter. |
| **`ST_Intersects(geometry, geometry)`** | Supported **if** each candidate can evaluate `Intersects` efficiently. Complex geometries increase per-node CPU; **cell/MBR prefilter** remains important. |
| **Lat/lon range predicates** | Natural **structured filters**; combine with cell bitmap selectivity estimation for strategy selection ([filtered_ann.md](../01_literature_survey/filtered_ann.md), [filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)). |

**Strategy table** (from [filtered_ann.md](../01_literature_survey/filtered_ann.md)): medium selectivity (~10–50%) is the **sweet spot** for ACORN-style traversal; very low selectivity favors prefilter/brute-force on the filtered set; very high selectivity favors post-filter HNSW.

## 4. Evidence

- **Problem framing**: Explicitly addresses **filtered-out neighbors** breaking greedy HNSW search ([filtered_ann.md](../01_literature_survey/filtered_ann.md)).
- **Adaptive threshold**: Reduces overhead when filters are weak (majority of nodes pass) ([filtered_ann.md](../01_literature_survey/filtered_ann.md)).
- **StarRocks-facing takeaway**: “Can be implemented within TenANN’s HNSW traversal” ([filtered_ann.md](../01_literature_survey/filtered_ann.md)); **planner** can map spatial selectivity to ACORN vs. fallback ([approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md)).

## 5. Proposed Spatial Optimizations

1. **S2 three-level check** before expensive predicates: cell outside region → skip; cell fully inside → accept; partial overlap → exact `ST_Contains` ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)).
2. **Selectivity from S2 covering**: Estimate σ = fraction of points in cells overlapping the query region to choose ACORN vs. prefilter vs. post-filter ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md), [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md)).
3. **Directed 2-hop** (project design): when σ is moderate, restrict second-hop candidates by vector closeness to q to limit fan-out ([filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)).
4. **ρ-aware planning**: For ρ ≈ 0 (location and embedding independent), ACORN matches the **filtered ANN** regime described in [hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md).

## 6. Verdict

**CONDITIONALLY SUITABLE**

ACORN is **well suited** to spatial predicates **as arbitrary filters** when selectivity is **not extreme** and predicate evaluation can be made cheap (cells, MBRs). It is **not** a replacement for **spatial indexing** when the workload is dominated by **highly selective** regions or heavy geometry — combine with **spatial-first** candidate generation or **partitioned** vector indexes in those cases ([filtered_ann.md](../01_literature_survey/filtered_ann.md), [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md)).

## 7. StarRocks Fit

- **Implementation path**: Extend TenANN HNSW **search** with predicate checks and controlled 2-hop expansion; store minimal spatial fields per node ([filtered_ann.md](../01_literature_survey/filtered_ann.md), [filtered_hnsw.md](../03_algorithmic_design/filtered_hnsw.md)).
- **Planner**: Wire **σ_s** (and optionally ρ̂) into pick of ACORN vs. cascade vs. partitioned HNSW — aligns with **Fallback: Filtered HNSW** in [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md).
- **Risk**: Worst-case **2-hop** blow-up under pathological filter shapes; mitigate with hop limits and spatial prefiltering.
