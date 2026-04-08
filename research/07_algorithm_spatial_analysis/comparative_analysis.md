# Comparative Analysis — Spatial + Vector Algorithms

This note synthesizes the sixteen per-algorithm notes in this folder into a **head-to-head view** on spatial axes, **scenario recommendations**, and a **ranked integration stance** for StarRocks (segment architecture, TenANN, existing S2 and planner work).

---

## 1. Spatial axes (how to read the “rankings”)

Algorithms are not always comparable on a single score: some are **storage indexes**, some are **query execution patterns**, and **spatial structures** are orthogonal **access paths** rather than ANN methods. The axes below separate those roles.

| Axis | What it measures | Stronger end |
|------|------------------|--------------|
| **Native geometry vs. filter** | Does the method *understand* regions, or only *filter* candidates? | Native region semantics (e.g., Mesh k-RANNS) vs. predicate-agnostic graphs (HNSW, ACORN) |
| **High spatial selectivity** | Small geographic footprint → tiny valid set | Spatial-first cascade, S2/R-tree pruning, **then** ANN on survivors |
| **Low spatial selectivity** | Large region / weak filter | Adaptive filtered traversal (ACORN), or vector-first with cheap rejects (S2 three-level) |
| **Axis-aligned lat/lon range** | Rectangle filters on scalars | UNIFY-style RF-ANNS, KHI, IVF² fusion, Compass cooperation |
| **Arbitrary OGC predicates** | Polygons, buffers, `ST_Intersects` | Classical **spatial structures** + refinement; graph methods stay **helpers** |
| **Disk scale** | Billion vectors, memory pressure | Vamana / DiskANN, Filtered-DiskANN, SPANN-style partitioning (spatial still external) |
| **Incremental ingestion** | Frequent updates without full rebuild | WoW, dynamic segment graphs (with caveats for immutable segments) |
| **Shipped / near-term fit** | TenANN + BE reality today | HNSW, IVF/IVFPQ, S2; ACORN as traversal extension |

---

## 2. Head-to-head ranking across spatial axes

Rough **ordinal** strength (1 = strongest on that axis for StarRocks-style hybrid GIS+ANN). Ties and “N/A” are called out.

### 2.1 Handling real geographic regions (not just IDs)

1. **Mesh** — Built for geo-tagged vectors under **spatial range** constraints; adaptive to selectivity.  
2. **Spatial structures (S2, R-tree, …)** — Correct **ST_*** semantics at the storage/geometry layer.  
3. **KHI / UNIFY / IVF²** — Strong for **numeric range** and discretized keys; polygons need covers or refinement.  
4. **ACORN / NaviX** — Predicate-agnostic **filters**; geometry is evaluated per candidate, not routed by the graph.  
5. **Base HNSW / Vamana** — Pure vector; spatial is always **around** the graph.

### 2.2 High spatial selectivity (tiny fraction of rows in region)

1. **Spatial-first cascade** (S2 cover → bitmap/rowset → ANN on candidates) — *pattern*, not one algorithm.  
2. **IVF²-style posting intersection** — When spatial keys map to inverted lists / bitsets.  
3. **KHI** — Prune attribute-space leaves before local ANN.  
4. **Mesh** — Adaptive branch favors spatial pruning when σ is small.  
5. **Filtered-DiskANN** — Strong **if** labels align with coarse regions (e.g., cells).  
6. **ACORN** — 2-hop helps **medium** σ; **very** high selectivity often prefers prefilter, not graph repair.

### 2.3 Low spatial selectivity (large fraction passes the filter)

1. **ACORN** (adaptive) or **vanilla HNSW + post-filter** when σ is high — avoids 2-hop tax.  
2. **Compass-style cooperation** — Alternate cheap structured passes with vector expansion.  
3. **NaviX-style adaptive local** — Same idea at execution level.  
4. **Mesh** — Adaptive query path explicitly considers large regions.

### 2.4 Lat/lon axis-aligned ranges (OLAP-friendly)

1. **UNIFY** (SIG/HSIG), **KHI**, **IVF²** — Natural **range-filtered** ANN story.  
2. **Compass** — B+-tree / zonemap-friendly **structured + vector** interleaving.  
3. **WoW** — When ranges map to **scalar windows** (possibly after S2/H3 encoding).  
4. **IVF/IVFPQ** — **Suit** when centroids/lists are **spatially** defined (else conditional).

### 2.5 Disk / memory pressure (very large vector columns)

1. **Vamana / DiskANN** — Disk-resident ANN; pair with external spatial pruning.  
2. **Filtered-DiskANN** — Labels + stitching; needs discrete spatial buckets.  
3. **SPANN** (grouped note) — SSD partitions + graph; **not** core TenANN today.  
4. **ScaNN** — Quantization + partitioning at scale; **low** StarRocks fit unless product mandates IP-at-scale.  
5. **In-memory HNSW** — TenANN default; bounded by RAM per segment.

### 2.6 Incremental updates (streaming, many small flushes)

1. **WoW** — Designed for **incremental** RFANN on windowed keys.  
2. **Dynamic segment graph** — Incremental RF-ANNS; **conflicts** with strict **immutable** segment freeze unless scoped to memtable/delta.  
3. **UNIFY (HSIG)** — Incremental segment flavor; same immutability caveat.  
4. **iRangeGraph** — **Poor** for high-frequency in-tree updates (batch rebuild fits StarRocks better).  
5. **Mesh** — Workload-optimal **batch** grouping; **periodic** rebuild vs. WoW-style streams (per notes).

---

## 3. Recommendations by use-case scenario

### 3.1 High spatial selectivity (small map region, tight polygon or small radius)

**Prefer:** **Spatial-first**: S2 (or future R-tree) **candidate generation** + **exact** `ST_*` refinement + **ANN on the surviving rowset** (TenANN HNSW/IVF on that subset or per-cell indexes).

**Use from this survey:** **Spatial structures** (16) as the **source of truth** for geometry; **IVF²** (13) and **KHI** (11) when postings/zones align with discretized keys; **Filtered-DiskANN** (3) only if **labels** (e.g., cells) are first-class and stable.

**Avoid relying on alone:** Raw **HNSW** (1) or **Vamana** (4) without prefilter — recall risk when vector similarity and geography diverge.

### 3.2 Low spatial selectivity (continent-sized box, weak filter)

**Prefer:** **Vector-first** with **cheap spatial rejects** (S2 three-level, MBR), **adaptive ACORN** (2) only in the **medium-σ** band; at very high pass rates use **post-filter HNSW** or **Compass** (6)-style cooperative batches.

**Use from this survey:** **ACORN** (2), **Compass** (6), **NaviX** (5) ideas in **planner + executor**; **Mesh** (10) adaptive query if the product invests in workload-optimal grouping.

### 3.3 Mixed workload (ranges + polygons + ad-hoc radii)

**Prefer:** **Layered architecture**: (1) **S2 / zone maps / bitmaps** for pruning and rowset intersection; (2) **TenANN** for ANN inside scoped sets; (3) **optimizer rules** (σ, ρ̂) choosing cascade vs. filtered graph vs. partitioned indexes — as in `approach_taxonomy.md`.

**Use from this survey:** **ACORN** (2) + **UNIFY/KHI/IVF²** (7, 11, 13) for the **range-heavy** slice; **Mesh** (10) as **design target** for unified geo+vector when engineering allows; **LIST** (14) only for **repeated** spatio-textual patterns with training budget.

### 3.4 Streaming ingestion (frequent inserts, many small segments)

**Prefer:** **Per-segment** immutable indexes (current StarRocks model) + **WoW** (9)-like **incremental RFANN** *inside* a mutable layer **or** frequent compaction with **acceptable rebuild cost**.

**Use from this survey:** **WoW** (9), **dynamic segment graph** (8) as **references**; **iRangeGraph** (12) is a **poor** match for row-level churn inside one tree. Validate against **segment seal** semantics — full online dynamic graphs are **research-forward** for the core BE.

---

## 4. Final ranked recommendation for StarRocks integration

Order reflects **impact × feasibility** for a **columnar OLAP** engine that already has **TenANN**, **segments**, and **S2**.

| Rank | Item | Role |
|------|------|------|
| **1** | **Spatial structures + cascade** ([16](16_spatial_structures.md), `cascade_pipeline.md`) | **Non-negotiable** for correct `ST_*`: S2 covers, bitsets, eventual R-tree — **prune row IDs** before ANN. |
| **2** | **HNSW (TenANN)** ([01](01_hnsw.md)) | **Default ANN core**; always combined with spatial metadata or filters for hybrid queries. |
| **3** | **ACORN-style filtered HNSW** ([02](02_acorn.md)) | **First graph extension** for **medium-selectivity** predicates without abandoning the shipped index. |
| **4** | **Compass-style cooperative execution** ([06](06_compass.md)) | **Planner + executor** pattern: structured access (zonemap, bitmap, spatial iterator) **interleaved** with vector search — low conceptual risk. |
| **5** | **Mesh** ([10](10_mesh.md)) | **North-star** for **k-RANNS** when product commits to **workload-optimal** spatial grouping and adaptive queries — **high** implementation cost. |
| **6** | **UNIFY / KHI / IVF²** ([07](07_unify.md), [11](11_khi.md), [13](13_ivf2.md)) | **Range-heavy** and **fusion** scenarios (postings ∩ IVF, attribute trees); lighter than full HSIG if **S2-partitioned HNSW** suffices. |
| **7** | **IVF / IVFPQ (TenANN)** ([15](15_ivf_ivfpq_scann_spann.md)) | Already **high fit**; spatially **stratified** training improves hybrid behavior. |
| **8** | **Filtered-DiskANN / Vamana** ([03](03_filtered_diskann.md), [04](04_vamana_diskann.md)) | **When** single-node RAM cannot hold HNSW — **after** spatial pruning story is solid. |
| **9** | **NaviX** ([05](05_navix.md)) | **Evidence** for adaptive-local filtered search — implement concepts in **TenANN + optimizer**, not a GDBMS port. |
| **10** | **WoW / dynamic segment graphs** ([09](09_wow.md), [08](08_dynamic_segment_graph.md)) | **Streaming** and **incremental RFANN** — prioritize **after** batch hybrid path is production-grade. |
| **11** | **iRangeGraph** ([12](12_irangegraph.md)) | **Niche**: dominant **1D** scalar RFANN with **batch** rebuild; less central than S2+HNSW for GIS. |
| **12** | **LIST** ([14](14_list.md)) | **Optional** learned router for **stable** spatio-textual workloads — not a general GIS replacement. |
| **13** | **ScaNN / SPANN** ([15](15_ivf_ivfpq_scann_spann.md)) | **Low priority** in core BE unless external scale/IP requirements appear; overlap with segments for SPANN. |

**Bottom line:** StarRocks should treat **spatial access paths (especially S2) + cascade planning** as the **foundation**, **TenANN HNSW/IVF** as the **vector engine**, and **ACORN + cooperative execution** as the **first-wave** upgrades for hybrid SQL. **Mesh** is the **leading academic target** for unified geo+vector structure but **costly**; **disk ANN** and **incremental window indexes** are **second-order** bets tied to scale and ingestion stories.

---

## References

Individual verdicts and evidence: [README.md](README.md) master table and files `01_hnsw.md`–`16_spatial_structures.md`. Cross-cutting design: [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md), [architecture_fit.md](../04_starrocks_design/architecture_fit.md), [cascade_pipeline.md](../03_algorithmic_design/cascade_pipeline.md).
