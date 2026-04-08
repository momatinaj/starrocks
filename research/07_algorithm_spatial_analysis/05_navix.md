# NaviX — Spatial Suitability Analysis

## 1. Algorithm Summary

**NaviX** is a native vector index for graph database management systems (GDBMSs) that embeds HNSW-style search into the storage and execution stack of the underlying engine, with an emphasis on **predicate-agnostic** filtered kNN: arbitrary selection predicates are applied to shrink the candidate set before or during vector search **Sehgal & Salihoglu**, *NaviX: A Native Vector Index Design for Graph DBMSs With Robust Predicate-Agnostic Search Performance*, Proceedings of the VLDB Endowment, Vol. 18, 2025 (PVLDB 18(11): 4438–4450). The design targets **disk-oriented** integration (leveraging GDBMS buffer management and operators) and introduces **adaptive-local** heuristics that adjust search behavior based on **local selectivity** in the HNSW graph neighborhood. See also the survey in [`research/01_literature_survey/filtered_ann.md`](../01_literature_survey/filtered_ann.md) (NaviX subsection).

## 2. Mechanism Deep-Dive

**Build (conceptual)**  
- Vectors are indexed with an HNSW (or HNSW-like) graph as a first-class storage structure inside the GDBMS.  
- Predicate metadata needed for filtering may live in the same system; the index itself is still primarily **vector-similarity** structure.

**Query**  
- A relational or graph query produces a **filter** (e.g., predicates on structured columns).  
- **Prefiltering** passes the qualifying subset into the kNN operator so distance work is restricted to rows that pass the filter (survey summary in [`filtered_ann.md`](../01_literature_survey/filtered_ann.md)).  
- **Adaptive-local** logic uses observed selectivity around the current search frontier to tune traversal (when to expand aggressively vs. prune), mitigating pathological cases where the graph appears disconnected under strict filters.

**Why it matters spatially**  
- Spatial predicates (`ST_*`) are just another class of filters from the algorithm’s perspective: if the engine can evaluate them and produce row IDs or iterators, NaviX’s integration story still applies.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **CONDITIONALLY suitable.** NaviX does not encode polygon geometry in the graph; containment must be evaluated by the execution engine (or a spatial index). Works if the GDBMS/OLAP layer can **prefilter** or **interleave** containment checks with kNN as in other predicate-agnostic designs. |
| **`ST_DWithin(point, point, r)`** | **CONDITIONALLY suitable.** Equivalent to a distance threshold on coordinates; can be pushed as a filter. Very selective disks favor prefilter + kNN; low selectivity approaches post-filter or hybrid (see selectivity table in [`filtered_ann.md`](../01_literature_survey/filtered_ann.md)). |
| **`ST_Intersects(geometry, geometry)`** | **CONDITIONALLY suitable.** Same as above: general geometries require exact tests or index-supported candidate generation; NaviX optimizes **vector + predicate**, not **spatial index structure** itself. |
| **Lat / Lon range predicates** | **CONDITIONALLY suitable.** Scalar ranges on indexed columns align well with **prefilter** paths and with adaptive-local behavior when selectivity varies across the graph. |

## 4. Evidence

- **Venue & scope**: PVLDB full paper on **native** integration in a GDBMS (Kùzu), demonstrating that **deep integration** of vector search with tabular/graph predicates is feasible with robust performance **Sehgal & Salihoglu, PVLDB 2025**.  
- **Predicate-agnostic claim**: The paper’s positioning matches the broader filtered-ANN literature: performance depends on **filter selectivity** and graph connectivity under the filter (compare strategy table in [`filtered_ann.md`](../01_literature_survey/filtered_ann.md)).  
- **Adaptive-local**: Qualitative benefit — reducing worst-case behavior when local neighborhoods have very different fractions of qualifying nodes; no substitute for a **spatial** index when the predicate is purely geometric and highly selective.

## 5. Proposed Spatial Optimizations

1. **Spatial index → row ID set → kNN**: Use R-tree, S2/H3 cell lists, or bitmap prefilter (as in [`cascade_pipeline.md`](../03_algorithmic_design/cascade_pipeline.md)) to produce candidates, then run vector search only on that set — aligns with NaviX-style **prefilter**.  
2. **S2 cell ID as a structured column**: Feed cell-based predicates into the same pipeline as other attributes; connect to **spatial-partitioned HNSW** ideas in [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md).  
3. **Selectivity-aware coupling**: Reuse NaviX’s **local selectivity** idea in the **optimizer**: combine estimated spatial selectivity with vector search cost (see correlation discussion in [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md)).  
4. **Avoid assuming GDBMS-specific APIs**: Port the *ideas* (adaptive local traversal + prefilter) into TenANN-backed search without requiring a full GDBMS embedding.

## 6. Verdict

**CONDITIONALLY SUITABLE**

NaviX is strong for **predicate-agnostic** kNN with **structured filters**, which includes **spatial filters** when the engine can evaluate them efficiently. It is **not** a dedicated spatial–vector hybrid index: **polygon intersection** and **complex geometry** still need classical spatial access paths or cascades. The **verdict** is conditional on combining NaviX-style execution with **spatial indexing or selective prefilter**, as summarized in [`filtered_ann.md`](../01_literature_survey/filtered_ann.md) and [`cascade_pipeline.md`](../03_algorithmic_design/cascade_pipeline.md).

## 7. StarRocks Fit

- **Alignment**: StarRocks already separates **segment scans**, **zone maps**, **bitmap/inverted** paths, and **TenANN** vector indexes ([`architecture_fit.md`](../04_starrocks_design/architecture_fit.md)). NaviX’s **native operator** philosophy maps to **cooperative** plans: spatial predicate evaluation + vector index read, not a monolithic GDBMS.  
- **Gap**: StarRocks is **columnar OLAP**, not Kùzu; reproducing “NaviX” as a product means **planner + executor** work (pushdown, delete filters, optional ACORN-style traversal per [`tenann_extensions.md`](../04_starrocks_design/tenann_extensions.md)), not dropping in a single index type.  
- **Practical takeaway**: Treat NaviX as **evidence** that **adaptive-local** filtered HNSW and **prefilter** integration matter; implement those concepts in **TenANN + optimizer** rather than porting the full Kùzu-specific design.
