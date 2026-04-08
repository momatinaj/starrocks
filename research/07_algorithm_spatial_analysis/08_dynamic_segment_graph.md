# Dynamic Segment Graph — Spatial Suitability Analysis

## 1. Algorithm Summary

The **Dynamic Segment Graph** targets **dynamic range-filtered approximate nearest neighbor search**: vectors arrive in **arbitrary order**, and the index must support **insertions** without frequent full rebuilds. It **compresses** multiple **HNSW**-style segment graphs into a **dynamic segment graph**, achieving **lossless** search capability under stated conditions while bounding index growth — notably **O(log n)** **expected new edges per insertion** in the asymptotic story **Peng et al.**, *Dynamic Range-Filtering Approximate Nearest Neighbor Search*, Proceedings of the VLDB Endowment, Vol. 18, 2025 (PVLDB 18(10): 3256–3268). Bibliography: [`bibliography.bib`](../01_literature_survey/papers/bibliography.bib) (`dynamic_segment_graph2025`). Survey summary: [`filtered_ann.md`](../01_literature_survey/filtered_ann.md) (Dynamic Segment Graph subsection); research log: [`ITERATION_LOG.md`](../ITERATION_LOG.md).

## 2. Mechanism Deep-Dive

**Problem setting**  
- **Range-filtered ANN**: queries specify a **vector** and a **range** on one or more **filter attributes** (same RF-ANNS family as **UNIFY**).  
- **Dynamic inserts**: unlike static batch-built HNSW, new points appear **online**.

**Core idea**  
- Maintain **segments** (partitions of the data by **range** on filter key(s)); each segment can host an **HNSW**-like graph.  
- **Compress** the family of segment graphs into a **single dynamic segment graph** with **shared structure** and **controlled edge growth**, preserving ANN quality under the paper’s assumptions.

**Query**  
- Identify **range overlap** with query filter → navigate **segment graph** + **vector** proximity → return approximate neighbors.

**Spatial analogy**  
- If filter attributes are **lat/lon** buckets or **1D projections**, segments correspond to **spatial strips** or **intervals**; arbitrary **polygons** are not first-class in the RF-ANNS formulation.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **CONDITIONALLY suitable.** Polygon filters are **not** native 1D ranges. Map to **S2 cell union**, **bbox**, or **tile IDs** as **range-like** segment keys; **exact** containment after ANN. Same pattern as UNIFY and **spatial-partitioned** designs [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md). |
| **`ST_DWithin(point, point, r)`** | **CONDITIONALLY suitable.** Use **conservative bbox** or **cell disk cover** for segment selection; **exact** geodesic check on candidates. |
| **`ST_Intersects(geometry, geometry)`** | **CONDITIONALLY suitable.** Requires **decomposition** to segment predicates or a **parallel spatial index**; the dynamic segment graph optimizes **vector + range**, not **general geometry**. |
| **Lat / Lon range predicates** | **SUITABLE** as **scalar range** RF-ANNS — aligns with the paper’s **range-filtered** setting and with incremental updates when new rows land in **known** range buckets. |

## 4. Evidence

- **Insertion cost**: Survey cites **substantially reduced index size increment** and **O(log n)** expected **new edges per insertion** under the paper’s model [`filtered_ann.md`](../01_literature_survey/filtered_ann.md).  
- **Query performance**: Preserves **ANN** quality compared to **naïve** multiple HNSW baselines (per VLDB evaluation — see primary paper for datasets and recall numbers).  
- **Spatial gap**: Reported workloads are **numeric range** filters; **geodesic** predicates and **polygon** shape complexity are **not** the paper’s focus — **evidence** for **OGC** workloads is **indirect**.

## 5. Proposed Spatial Optimizations

1. **S2 cell as segment key**: Assign each vector a **primary S2 cell ID** at fixed level as the **range dimension** for segment graph routing — connects to **spatial-partitioned HNSW** [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md).  
2. **Multi-resolution segments**: For skewed geo data, **rebalance** segment boundaries using **quantiles** on lat/lon (similar spirit to **dynamic** maintenance).  
3. **Compaction coupling**: StarRocks **immutable segments** mean **true online** dynamic graph may apply **within** a **mutable memtable** or **delta** layer, then **freeze** at segment flush — align with compaction discussion in [`architecture_fit.md`](../04_starrocks_design/architecture_fit.md).  
4. **Hybrid with cascade**: For **complex polygons**, use **spatial index → row IDs** first when selectivity is tiny ([`cascade_pipeline.md`](../03_algorithmic_design/cascade_pipeline.md)); use **dynamic segment graph** when **range** selectivity is **moderate** and **streaming** insertions dominate.

## 6. Verdict

**CONDITIONALLY SUITABLE**

- **Strong fit** for **lat/lon range** + **incremental** RF-ANNS in systems that **mutate** indexes frequently.  
- **CONDITIONAL** for **`ST_*` geometry**: requires **mapping** spatial predicates to **segment keys** or **hybrid** execution; not a **complete spatial database** solution on its own.

For **StarRocks** specifically, **immutable segments** temper the value of **online** dynamic graphs unless applied to **pre-compaction** layers or **streaming** scenarios — see **StarRocks Fit**.

## 7. StarRocks Fit

- **Streaming / real-time loads**: If future designs allow **more granular** vector index updates before segment seal, dynamic segment graphs address **edge blow-up** from **naïve multi-HNSW** per range bucket [`ITERATION_LOG.md`](../ITERATION_LOG.md).  
- **Current architecture**: Segments are **immutable**; full dynamic graph **maintenance** conflicts with **rebuild-on-compaction** for `.hvi` / TenANN ([`architecture_fit.md`](../04_starrocks_design/architecture_fit.md)). **Practical fit** is **research-forward** or **limited to in-memory** / **delta** structures.  
- **TenANN**: Building a **dynamic segment graph** would require **new on-disk format** and **search** code paths beyond current **HNSW** builder/reader — **high** engineering cost.  
- **Recommendation**: Treat as **strategic reference** for **incremental RF-ANNS** and **compressed multi-graph** ideas; near-term **production** path remains **spatial prefilter + TenANN** or **S2-partitioned per-segment graphs** ([`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)).
