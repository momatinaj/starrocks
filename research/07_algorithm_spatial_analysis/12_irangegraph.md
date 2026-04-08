# iRangeGraph (Elemental Graphs + Segment Tree)

## 1. Algorithm Summary

**iRangeGraph** addresses **range-filtering approximate nearest neighbor search (RFANN)** where each object is **(vector, scalar attribute)** and queries specify a **numeric interval** on the scalar plus **k-NN** on the vector. Xu et al., *iRangeGraph: Improvising Range-dedicated Graphs for Range-filtering Nearest Neighbor Search*, **arXiv:2409.02571** (Sep 2024). Instead of materializing **O(n²)** dedicated graphs for all possible query ranges (as in prior compressed approaches), iRangeGraph **materializes RNG-style elemental graphs** for **segment-tree** nodes — a **moderate** number of ranges — and **assembles** a **query-specific dedicated graph** at search time. Related RFANN surveys in-repo: [`filtered_ann.md`](../01_literature_survey/filtered_ann.md), [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md); **Dynamic Segment Graph** (PVLDB 2025) discusses **iRange** / segment-tree family in related work. **DIGRA** (SIGMOD 2026) notes iRangeGraph’s **static** segment tree limits **dynamic updates** — relevant for operational fit.

## 2. Mechanism Deep-Dive

**Index phase**

1. Sort or order objects by **scalar attribute** φ (e.g., timestamp, price, z-ordered spatial key).
2. Build a **balanced segment tree** over the **index order**; each tree node corresponds to a **contiguous segment** of objects in φ-space.
3. For **each segment**, materialize an **elemental graph** (paper uses **RNG-based** approximate graph construction following state-of-the-art graph ANN practice).

**Query phase**

1. Given **q**, **k**, and query range **[l, r]** on φ:
2. **Decompose** **[l, r]** into **O(log n)** canonical segment-tree nodes that **cover** the range.
3. **Combine** elemental graphs from those nodes into a **dedicated graph** for this query (paper analyzes **low** construction time for this step).
4. Run **greedy graph search** (HNSW-like) on the **dedicated** structure so all visited candidates satisfy **φ ∈ [l, r]**.

**Contrast:** **UNIFY** uses **SIG/HSIG** inclusive graphs ([`filtered_ann.md`](../01_literature_survey/filtered_ann.md)); **iRangeGraph** emphasizes **space–time tradeoff** via **few** stored graphs + **query-time** composition.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **Not directly native.** Polygons are **2D** regions; iRangeGraph’s segment tree is **1D** over a **total order** on φ. **Unless** φ encodes a **space-filling curve** or **cell id order**, containment does not map to **one contiguous interval** — it maps to **many intervals** or **lossy outer MBR**. |
| **`ST_DWithin(point, point, r)`** | **Same caveat.** A **disk** is not one **1D interval** in arbitrary lon/lat ordering; **bounding annulus** hacks are **imprecise** without multi-interval decomposition. |
| **`ST_Intersects(geom, geom)`** | **Weak direct fit** for arbitrary WKB/WKT; **feasible** when reduced to **interval sets** on a **linearized** spatial key. |
| **Lat/lon range predicates** | **Partial fit.** A **lon × lat box** is **two** independent ranges — iRangeGraph as published targets **one** scalar φ. **Extension** would need **multi-attribute** RFANN (see **KHI**) or **separate** segment trees per dimension (non-trivial). **Single-axis** range (e.g., `lon BETWEEN …`) works if φ = lon only, at the cost of **correlating** poorly with **2D** locality. |

## 4. Evidence

- **Construction thesis:** Abstract (arXiv:2409.02571) argues **elemental graphs** for **moderate** segment-tree ranges avoid **lossy** compression of **all** query ranges while keeping **query-time** assembly **cheap**.
- **Empirical:** Third-party summaries report **2×–5×** QPS gains vs. baselines at high recall on some workloads (treat as **indicative**; confirm on StarRocks-like data).
- **In-repo context:** [`filtered_ann.md`](../01_literature_survey/filtered_ann.md) clusters RFANN work (**UNIFY**, **WoW**, **Dynamic Segment Graph**); iRangeGraph occupies the **segment-tree + graph** niche between **full materialization** and **pure post-filter**.
- **Update limitation:** Follow-on **DIGRA** discussion (external SIGMOD 2026 paper) states iRangeGraph is **static** — segment trees are **hard to update** incrementally; aligns with choosing **batch** StarRocks segments for this index style.

## 5. Proposed Spatial Optimizations

1. **Scalarize space:** Map location to **Hilbert** or **Morton** order at fixed precision; treat RFANN range as **interval on curve** — works for **approximate** spatial filtering with **exact geometry post-check** (same pattern as **WoW** spatial key proposal in [`09_wow.md`](09_wow.md)).
2. **Multi-tree KHI hybrid:** Use **KHI**-style attribute tree for **2D** bounds and **iRangeGraph-style** subgraphs at leaves for **1D** sub-orderings — avoid forcing **2D** regions into **one** segment tree.
3. **Cell-local iRangeGraph:** Inside each **S2 cell** ([`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)), build iRangeGraph on **local** φ (e.g., secondary sort key) — keeps **1D** order **meaningful** within **tiled** geography.
4. **Read-only segments:** Align with StarRocks **immutable segments** — rebuild segment tree on **compaction**; avoid dynamic single-tree updates.

## 6. Verdict

**CONDITIONALLY SUITABLE**

iRangeGraph is **strong** for **single scalar RFANN** with **batch-built** data and **range-heavy** workloads. For **full GIS**, it is **not** a standalone solution: **polygons**, **buffers**, and **multi-axis** lat/lon boxes require **encoding tricks**, **multi-index** fusion, or **post-filters**. The verdict is **conditional** on **acceptable approximation** of spatial predicates via **1D ordering** or **hierarchical decomposition**.

## 7. StarRocks Fit

- **TenANN:** **Elemental graphs** per segment-tree node are **additional** on-disk structures beside **per-segment HNSW**; assembly at query time adds **CPU** cost — viable for **selective** ranges where composed graph is **small**.
- **Segments:** **Static** iRangeGraph aligns with **immutable** `.vi` / vector index segments; **rebuild** on major compaction matches known **update** limitations.
- **Planner:** Best when **one dominant scalar** (time, price, z-order key) **co-occurs** with vector ANN; **pure map geometry** workloads favor **Mesh**, **S2-partitioned HNSW**, or **ACORN** ([`research/README.md`](../README.md)).
- **Incremental:** **Poor** for high-frequency row-level upserts **inside** one segment tree — prefer **WoW** or **Dynamic Segment Graph** streams ([`filtered_ann.md`](../01_literature_survey/filtered_ann.md)) for **mutable** RFANN.
