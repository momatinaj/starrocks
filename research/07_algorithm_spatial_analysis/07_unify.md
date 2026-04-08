# UNIFY — Spatial Suitability Analysis

## 1. Algorithm Summary

**UNIFY** addresses **range-filtered approximate nearest neighbor search (RF-ANNS)** over high-dimensional vectors when **continuous scalar attributes** define filters (e.g., numeric ranges). It introduces the **Segmented Inclusive Graph (SIG)**, which **partitions** the dataset by attribute values and connects segments so that search can use **pre-, post-, or hybrid filtering** in one structure; **Hierarchical SIG (HSIG)** adds hierarchy for **logarithmic** navigation and **incremental insertion** **Liang et al.**, *UNIFY: Unified Index for Range Filtered Approximate Nearest Neighbors Search*, Proceedings of the VLDB Endowment, Vol. 18, 2024 (PVLDB 18(4): 1118–1130). Bibliography entry: [`research/01_literature_survey/papers/bibliography.bib`](../01_literature_survey/papers/bibliography.bib) (`unify2024`). Survey: [`filtered_ann.md`](../01_literature_survey/filtered_ann.md) (UNIFY subsection).

## 2. Mechanism Deep-Dive

**SIG**  
- Data are **segmented** by **1D (or attribute-wise) ranges**; each segment holds a subgraph (HNSW-like proximity structure) with **inclusive** edges so that neighbors relevant across **range boundaries** remain reachable.  
- A query specifies a **vector** and a **range** on the filtering attribute(s); the algorithm chooses **prefiltering** (restrict to segments overlapping the range), **post-filtering** (explore then prune), or **hybrid** navigation depending on selectivity.

**HSIG**  
- A **hierarchy** over segments supports coarse-to-fine navigation and **incremental** updates without rebuilding a monolithic graph.

**Spatial reading**  
- **Scalar latitude/longitude** filters behave like the **continuous range** assumptions in the paper. **Polygons** do not align with a single axis-aligned 1D range.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **CONDITIONALLY suitable.** A polygon induces a **non-rectangular** region in (lat, lon). UNIFY’s native model is **range(s) on filter attribute(s)**. Approximate mapping: **S2/H3 cell covering** → treat cell IDs or **bounding box** as range predicates → SIG/HSIG over **discretized** keys; exact containment still needs **refinement** (see [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)). |
| **`ST_DWithin(point, point, r)`** | **CONDITIONALLY suitable.** On a projected plane or small area, **bounding box** + distance refinement works; **great-circle** distance is not a single axis-aligned box — still workable with **conservative bbox** + exact filter. |
| **`ST_Intersects(geometry, geometry)`** | **CONDITIONALLY suitable.** Same as containment: **decompose** to cells/bbox predicates or use a **spatial index** upstream; UNIFY handles the **vector** side after **spatial candidate** generation. |
| **Lat / Lon range predicates** | **SUITABLE.** Axis-aligned **latitude** and **longitude** ranges match RF-ANNS directly; HSIG supports **incremental** data if lat/lon-driven segments are updated carefully (distribution shifts may require **rebalancing**). |

## 4. Evidence

- **Complexity claims**: HSIG aims for **efficient hybrid filtering** with **hierarchical** segment navigation — favorable for **multi-segment** queries overlapping several lat/lon buckets **Liang et al., PVLDB 2024**.  
- **Structural parallel**: The survey explicitly connects UNIFY segmentation to **partitioning HNSW by S2 cell ranges** [`filtered_ann.md`](../01_literature_survey/filtered_ann.md).  
- **Limitation**: Published **evidence** centers on **numeric range** workloads, not **PostGIS-style** geometry; **recall** under **cell approximation** for polygons is an **engineering** concern, not proven by the original RF-ANNS benchmarks.

## 5. Proposed Spatial Optimizations

1. **2D → ordered key**: Encode **space-filling curve** (Z-order/Morton) on quantized lat/lon into a **scalar segment key** so SIG/HSIG operates on **one** ordering while preserving some spatial locality — tradeoffs vs. true 2D structures discussed in [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md).  
2. **Dual-attribute SIG**: Maintain **nested** or **tensor** segmentations for **lat** and **lon** (or HSIG over **rectangular** tiles) — increases index complexity; compare **KHI**-style attribute trees in the same survey [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md).  
3. **Refinement layer**: Always apply **exact** `ST_*` on top-k candidates after SIG-guided ANN ([`cascade_pipeline.md`](../03_algorithmic_design/cascade_pipeline.md)).  
4. **Segment alignment with StarRocks**: Map **HSIG segments** to **S2 cells** inside each **storage segment** to limit cross-talk at compaction boundaries ([`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)).

## 6. Verdict

**CONDITIONALLY SUITABLE**

- **SUITABLE** for **lat/lon range** + ANN when filters are **axis-aligned** and segment construction follows **scalar range** semantics.  
- **CONDITIONALLY SUITABLE** for **`ST_Contains` / `ST_Intersects` / `ST_DWithin`** when **spatial predicates** are **reduced** to **cell lists, bbox, or multi-range** predicates; **not** a drop-in replacement for a **general polygon index** without that layer.

## 7. StarRocks Fit

- **Conceptual fit**: UNIFY’s **unified pre/post/hybrid** story matches optimizer needs for **unknown selectivity** — similar to **ACORN**-style adaptivity but with **structure** in the index ([`filtered_ann.md`](../01_literature_survey/filtered_ann.md)).  
- **Engineering cost**: Implementing SIG/HSIG in C++ alongside **TenANN** is **non-trivial**; a lighter path is **S2-partitioned HNSW** per [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md), which borrows UNIFY’s **segmentation** idea without full HSIG.  
- **Immutability**: StarRocks segments are **immutable** until compaction; **incremental** HSIG benefits matter more for **streaming** tables — align with **dynamic** graph work in [`08_dynamic_segment_graph.md`](08_dynamic_segment_graph.md).  
- **Recommendation**: Use UNIFY as **design reference** for **range-filtered** hybrid graphs; prioritize **S2 + per-cell HNSW** if implementation bandwidth is limited ([`architecture_fit.md`](../04_starrocks_design/architecture_fit.md)).
