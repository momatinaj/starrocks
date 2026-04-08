# Compass — Spatial Suitability Analysis

## 1. Algorithm Summary

**Compass** targets **general filtered search** over **high-dimensional vectors** and **structured relational data** without inventing a single new composite index. It orchestrates **cooperative query execution** among existing structures: **HNSW** and **IVF** for vectors, and **B+-trees** (and similar) for structured attributes, using a **shared candidate queue** to interleave candidate generation and predicate evaluation **Ye et al.**, *Compass: General Filtered Search across Vector and Structured Data*, arXiv:2510.27141 (2025). The approach supports **conjunctions, disjunctions, and range predicates** on structured columns while performing ANN. Survey context: [`research/01_literature_survey/filtered_ann.md`](../01_literature_survey/filtered_ann.md) (Compass subsection); taxonomy placement under cooperative execution in [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md).

## 2. Mechanism Deep-Dive

**Indexes used**  
- **Vector side**: HNSW or IVF — standard approximate nearest neighbor access.  
- **Structured side**: B+-trees (or equivalent) for ranges and equality on scalar attributes.

**Execution**  
- **Cooperative execution**: Multiple index access paths **feed a common priority / candidate queue**; the scheduler pulls candidates, applies **vector distance** and **structured predicates**, and avoids committing to a pure “filter-first” or “ANN-first” ordering when either side would be suboptimal.  
- **Generality**: Spatially, any predicate that can be indexed or evaluated on structured columns can participate in the same framework **as long as** the engine exposes efficient access paths.

**Query shape**  
- Typical pattern: `WHERE structured_predicates AND vector ORDER BY distance LIMIT k` with cooperative blending of probes from B+-trees and vector indexes.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **CONDITIONALLY suitable.** A B+-tree alone does not index arbitrary polygons. If **lat/lon** are scalar columns, you can bound a **bounding box** via range predicates and refine `ST_Contains`; for production, **R*-tree**, **S2 cell** predicates, or **two-dimensional indexes** pair better with Compass’s cooperative pattern than a single 1D B+-tree. |
| **`ST_DWithin(point, point, r)`** | **CONDITIONALLY suitable.** Bounding-box prefilter + exact haversine check fits **range + residual predicate**; cooperative execution can alternate **spatial candidate generation** (where implemented) with **vector** expansion. |
| **`ST_Intersects(geometry, geometry)`** | **CONDITIONALLY suitable.** Depends on **spatial index** or decomposition (grid/cells); Compass’s contribution is **execution**, not a new **spatial algebra**. |
| **Lat / Lon range predicates** | **SUITABLE (as structured ranges).** Axis-aligned **latitude/longitude ranges** map directly to B+-tree or zone-map style access — the natural sweet spot for Compass-style **structured + vector** cooperation. |

## 4. Evidence

- **Design point**: The paper argues that **reusing** mature indexes beats monolithic unified structures for **diverse workloads**; the literature survey notes Compass’s **cooperative execution** between vector graphs and B+-trees [`filtered_ann.md`](../01_literature_survey/filtered_ann.md).  
- **Workload diversity**: Reported gains depend on **predicate mix** and **selectivity**; the filtered-ANN literature consistently shows **medium selectivity (~10–50%)** as the regime where **graph + filter** cooperation pays off (strategy table in [`filtered_ann.md`](../01_literature_survey/filtered_ann.md)).  
- **Spatial caveat**: Evidence in Compass is framed around **relational** structured attributes; **OGC geometry** predicates require **spatial** access methods — the **cooperative** idea still applies, but **evidence** for raw `ST_*` on complex types is indirect.

## 5. Proposed Spatial Optimizations

1. **Replace or augment B+-tree with spatial access**: For location columns, use **R-tree**, **S2 cell ID** indexed columns, or **H3** cell predicates so cooperative execution pulls **spatially relevant** batches (aligned with [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)).  
2. **Two-phase refinement**: **Coarse** spatial filter (cell bitmap / bbox) → **Compass-style** interleaved HNSW + exact `ST_*` refinement on survivors ([`cascade_pipeline.md`](../03_algorithmic_design/cascade_pipeline.md)).  
3. **Selectivity-driven scheduling**: Use **spatial histograms** (e.g., per-S2-cell counts) to decide whether the **vector** or **spatial** side drives the queue first — mirrors Compass’s intent with **spatial** replacing generic B+-tree statistics.  
4. **Segment-local cooperation**: Within each **segment**, run cooperative batches; **merge top-k** across segments ([`architecture_fit.md`](../04_starrocks_design/architecture_fit.md)).

## 6. Verdict

**CONDITIONALLY SUITABLE**

Compass is **SUITABLE** for **lat/lon range** + ANN when structured access paths exist. For **full OGC spatial types** (`ST_Contains`, `ST_Intersects`), it is **CONDITIONALLY SUITABLE**: the **algorithm** is a **query execution pattern**, not a spatial index; **suitability** hinges on plugging in **appropriate spatial indexes** or **cell prefilters** alongside TenANN. This matches the survey’s placement of Compass as **cooperative** fusion rather than geometry-aware graph redesign [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md).

## 7. StarRocks Fit

- **High fit conceptually**: StarRocks already combines **zone maps**, **bitmap/inverted indexes**, **table scans**, and **TenANN** — a **cooperative executor** layer is an incremental architectural step, not a new storage format.  
- **Implementation surface**: Planner rules to detect **spatial + vector ORDER BY … LIMIT**; runtime **shared candidate queue** between **spatial iterator** (or zonemap-pruned scan) and **HNSW** reader; optional **ACORN-style** traversal inside TenANN for medium-selectivity filters ([`tenann_extensions.md`](../04_starrocks_design/tenann_extensions.md)).  
- **Per-segment reality**: No cross-segment graph traversal — **merge top-k** per segment matches [`architecture_fit.md`](../04_starrocks_design/architecture_fit.md); Compass-style cooperation is **intra-segment and per-tablet** in practice.  
- **Risk**: **Low** for prototyping **execution** patterns; **medium** for full **optimizer** cost modeling across spatial + vector.
