# WoW (Window-to-Window Incremental Index)

## 1. Algorithm Summary

**WoW** is a range-filtering approximate nearest neighbor (RFANNS) index that supports **fully incremental, unordered insertions** without reorganizing the global data layout. Wang et al. organize the indexed set using **hierarchical window graphs** at multiple window granularities, coordinated by a **Weighted Balanced Tree (WBT)**, so that updates remain efficient while queries can still navigate a structure aligned with arbitrary numeric range filters. The work is documented in Wang et al., *WoW: A Window-to-Window Incremental Index for Range-Filtering Approximate Nearest Neighbor Search* (SIGMOD 2026 / arXiv:2508.18617, Aug 2025). The survey in [`research/01_literature_survey/filtered_ann.md`](../01_literature_survey/filtered_ann.md) summarizes WoW as achieving roughly **4× faster queries** than prior incremental RFANN indexes, with insertion complexity reported on the order of **O(log² n)** per update. See also [`research/01_literature_survey/papers/bibliography.bib`](../01_literature_survey/papers/bibliography.bib) entry `wow2025`.

## 2. Mechanism Deep-Dive

**Build / insert path**

1. Each vector carries one or more **scalar attributes** used for range filtering (the RFANNS setting).
2. WoW maintains **windows**—contiguous ranges over the attribute domain—and associates **window graphs** whose connectivity supports ANN search when the active query range aligns with that window.
3. A **WBT** balances window structures as points arrive in arbitrary order, avoiding full rebuilds while keeping tree height under control.
4. Inserts update only **local** window representations and graph edges as needed, rather than reshuffling global ordering.

**Query path**

1. Given query vector **q** and filter range **[L, R]** on the scalar attribute(s), the planner identifies which window(s) intersect the range.
2. Search starts from appropriate entry points in the hierarchy and runs greedy graph traversal (HNSW-style behavior at the window level) while **discarding** neighbors outside **[L, R]**.
3. Results are approximate nearest neighbors among vectors satisfying the range predicate.

The mental model matches **UNIFY** / **Dynamic Segment Graph** territory (segmented structures for RF-ANNS), but WoW optimizes the **incremental maintenance** story specifically. See the RFANN cluster discussion in [`research/01_literature_survey/README.md`](../01_literature_survey/README.md) and iteration notes in [`research/ITERATION_LOG.md`](../ITERATION_LOG.md).

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **Indirect.** WoW assumes **interval range(s)** on ordered scalar attribute(s). A polygon does not map to a single contiguous 1D range unless the geometry is approximated (e.g., S2 cell IDs, geohash prefixes, or a space-filling curve key). Containment then becomes **predicate translation** + possible **multiple disjoint ranges**. |
| **`ST_DWithin(point, point, r)`** | **Indirect.** A metric disk in lat/lon is not a single axis-aligned scalar interval. Bounding **circles** with min/max lat/lon boxes yields **over-filtering** (post-filter) or **multiple windows**. WoW can help if the engine exposes **derived scalar bounds** (e.g., great-circle distance bracketing) but not as a native 2D operator. |
| **`ST_Intersects(geom, geom)`** | **Weak fit by default.** Arbitrary geometry intersection reduces to complex regions in 2D; WoW’s window abstraction is **1D range-oriented**. Intersection against **rectangles** aligned with indexed axes is easier than arbitrary polygons. |
| **Lat/lon range predicates** | **Strong alignment when expressed as ranges.** Filters like `lat BETWEEN … AND … AND lon BETWEEN … AND …` map to **orthogonal range constraints**. WoW as published emphasizes **scalar RFANN**; **multi-attribute** range AND is related to **KHI**-style partitioning rather than pure WoW. Treat **lon/lat box** as two attributes or as a **precomputed spatial key** for best fit. |

## 4. Evidence

- **Incremental vs. batch:** [`filtered_ann.md`](../01_literature_survey/filtered_ann.md) cites **4×** query speedup vs. prior incremental RFANN indexes and highlights **O(log² n)**-style incremental maintenance (per paper claims).
- **Problem positioning:** [`research/01_literature_survey/README.md`](../01_literature_survey/README.md) groups WoW with **Dynamic Segment Graph** and **IVF²** under “range-filtered ANN” — i.e., **selectivity-adaptive** and **incremental** RF-ANNS are active research threads.
- **StarRocks ingestion angle:** [`ITERATION_LOG.md`](../ITERATION_LOG.md) flags WoW as relevant to **continuous loading** (e.g., stream-style batches), analogous to StarRocks load pipelines.

## 5. Proposed Spatial Optimizations

1. **Spatial key layer:** Map each row’s location to an **order-preserving scalar** (Hilbert/Z-order of quantized lat/lon, S2 cell id at fixed level, or geohash). Run WoW on that key so “windows” approximate spatial slabs; **refine** with exact geometry predicates.
2. **Dual-axis decomposition:** For box queries, either (a) nest WoW-like structures per dimension in the planner, or (b) adopt **KHI**-style attribute-space trees for 2D+ ranges and use WoW only for **single-axis** streams (see [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)).
3. **Selectivity hook:** Reuse WoW’s hierarchical windows as a **statistics-aware** path: small spatial range → narrow windows → fast graph entry; large range → fall back to **filtered HNSW (ACORN)** or spatial-first cascade (see strategy table in [`filtered_ann.md`](../01_literature_survey/filtered_ann.md)).
4. **Segment-local indexes:** Build WoW **per tablet/segment** so incremental compaction aligns with **append-only** segments; merge results across segments like partitioned HNSW.

## 6. Verdict

**CONDITIONALLY SUITABLE**

WoW is a strong match for **incremental RFANN on scalar (or scalarized spatial) keys** and for **workloads where spatial predicates reduce to ordered range predicates** after preprocessing. It is **not** a drop-in replacement for **full 2D GIS semantics** (polygons, buffers, general `ST_Intersects`) without an additional spatial access path or key encoding. Pair with **S2/H3**, **R-tree**, or **post-filter** geometry evaluation for correctness.

## 7. StarRocks Fit

- **TenANN / segments:** WoW’s incremental story aligns with **frequent segment flushes** and **many small immutable segments**, provided the implementation scope is **per-segment or per-partition** graphs rather than one global mutable graph.
- **Overlap with shipped work:** StarRocks already explores **ACORN** and **S2-partitioned HNSW** ([`research/README.md`](../README.md)). WoW would complement **ingestion-heavy** tables where **rebuilding** monolithic HNSW per compaction is too costly; hybrid planning (WoW-like incremental layer + periodic compaction to Mesh/partitioned HNSW) is plausible.
- **Implementation cost:** Moderate-to-high — requires **window graph** + **WBT** maintenance integrated with the columnar storage lifecycle; geometry remains in the **executor** unless keys are precomputed.
- **Incremental column (survey table):** **Yes** — WoW’s primary differentiator is **incremental** RFANN vs. static segment structures (contrast **iRangeGraph** static segment tree in related literature).
