# Classical Spatial Structures (R-Tree Family, Space-Partitioning Trees, Grids, S2, H3)

This entry evaluates **non-vector** spatial access methods as **filters**, **partition keys**, or **compound-index** shells for hybrid spatial+vector queries. It is **not** a single ANN algorithm; it is the **geometric substrate** surveyed in `research/01_literature_survey/spatial_indexes.md` and used in designs such as `research/03_algorithmic_design/r_tree_plus_hnsw.md` and `spatial_partitioned_hnsw.md`.

---

## 1. Algorithm Summary

**R-tree family** (Guttman **SIGMOD 1984**; **R\***-tree Beckmann et al. **SIGMOD 1990**): balanced trees of **minimum bounding rectangles (MBRs)**; search visits nodes whose MBRs overlap the query region (**`papers/bibliography.bib`**: `rtree1984`, `rstar1990`). **Space-partitioning trees** (e.g., **kd-tree** Bentley 1975, **quad-tree**): recursively split embedding of space (2D/3D). **Grid / hierarchical encodings**: **geohash**, **Google S2** (`s2geometry` in bibliography), **Uber H3**—map locations to **hierarchical cell IDs** for prefix/range scans (`research/01_literature_survey/spatial_indexes.md`).

---

## 2. Mechanism Deep-Dive

### R-tree / R\*-tree

- **Build**: Insert points/rectangles; choose subtree by minimal enlargement; split on overflow (R\* adds reinsertion).
- **Query**: Depth-first or priority queue; **prune** subtrees with **no MBR intersection** with query window/polygon MBR.

### KD-tree / Quad-tree

- **Build**: Axis-aligned splits (kd) or quadrant splits (quad).
- **Query**: Traverse cells intersecting the query; **exact** tests on points in leaves.

### S2 / H3 / Geohash

- **Build**: Compute **cell ID** from lat/lon at chosen resolution.
- **Query**: **Cover** region with a set of cells; **union** postings; refine exact geometry.

---

## 3. Spatial Query Suitability

Structures are evaluated **as spatial indexes** (vector ANN is assumed separate unless composed).

| Query type | R-tree / R\* | KD / Quad | Geohash | S2 | H3 |
|------------|--------------|-----------|---------|----|----|
| **ST_Contains(polygon, point)** | **SUITABLE**: traverse overlapping MBRs; **exact test** on candidates. | **SUITABLE** in 2D for points; degrades if unbalanced. | **CONDITIONALLY SUITABLE**: prefix scan + boundary pain. | **SUITABLE**: **cover** polygon with cells + point-in-poly on candidates. | **SUITABLE** (same pattern as S2). |
| **ST_DWithin** | **SUITABLE**: query **expanded MBR** / circle bbox. | **SUITABLE** with bbox pruning + exact distance. | **CONDITIONALLY SUITABLE**. | **SUITABLE**: annulus/circle covering at fine res. | **SUITABLE**. |
| **ST_Intersects** | **SUITABLE** for MBR filter + **exact** intersect on candidates. | **CONDITIONALLY SUITABLE** (complex shapes need decomposition). | **CONDITIONALLY SUITABLE**. | **SUITABLE** with covering + exact. | **SUITABLE**. |
| **Lat/lon range** | **SUITABLE** (rectangle query). | **SUITABLE**. | **SUITABLE** (with care at poles/dateline). | **SUITABLE** (cell union). | **SUITABLE**. |

---

## 4. Evidence

- **Spatial survey table** (`spatial_indexes.md`): **R\*** high integration potential; **S2** very high—**already in StarRocks** (`be/src/geo/`); **H3** strong but **new dependency**.
- **Complexity**: R-tree search **average** \(O(\log n)\), **worst** \(O(n)\) with heavy overlap; hierarchical cells trade **exactness** for **prefix-friendly** pruning.
- **Hybrid designs** (`r_tree_plus_hnsw.md`): R-tree leaves + **per-leaf HNSW** gives **arbitrary predicates** at cost of **implementation complexity**.

---

## 5. Proposed Spatial Optimizations (for hybrid vector queries)

1. **S2-first in StarRocks**: Use **cell covering** + **bitmap** row filters → pass **delete filter** to TenANN (`cascade_pipeline.md`).
2. **R-tree + vector**: **Bulk-load R\*** on segment bbox workload; attach **HNSW** only where **leaf cardinality** exceeds threshold (`r_tree_plus_hnsw.md`).
3. **Adaptive resolution**: Coarser S2 level for **sparse** regions, finer for **dense** (survey open questions).
4. **Dateline / pole handling**: Prefer **S2/H3** over naive lon ranges for global datasets.

---

## 6. Verdict

**SUITABLE** (as **spatial access paths** for `ST_*` and ranges)

**Caveat:** This verdict applies to **spatial structures themselves**. **Composition** with ANN (e.g., R-tree + HNSW) is **CONDITIONALLY SUITABLE** depending on **maintenance cost**, **leaf sizing**, and **segment** layout—see `r_tree_plus_hnsw.md`.

---

## 7. StarRocks Fit

| Structure | Fit | Notes |
|-----------|-----|-------|
| **S2** | **Excellent** | Already integrated; hierarchical IDs; cell covering for polygons (`spatial_indexes.md`). |
| **R-tree** | **Medium** | No first-class storage R-tree in core BE path per `r_tree_plus_hnsw.md`; would be **new** infrastructure. |
| **H3** | **Medium** | Popular for analytics; adds dependency; evaluate vs. S2 coverage. |
| **Geohash** | **Medium** | Simple string keys; **non-uniform** cell area vs latitude. |
| **KD-tree** | **Medium** | Great for **2D** in-memory filters; less standard for **on-disk** columnar segments than S2 bitsets. |

**TenANN alignment:** Spatial structures **do not replace** HNSW/IVF; they **prune row IDs** or **define partitions** for **vector search scope** (`approach_taxonomy.md`: cell-based vs geometry-based representation).

**References:** `research/01_literature_survey/spatial_indexes.md`; `research/01_literature_survey/papers/bibliography.bib` (`rtree1984`, `rstar1990`, `s2geometry`, `h3`); `research/03_algorithmic_design/r_tree_plus_hnsw.md`; `research/03_algorithmic_design/spatial_partitioned_hnsw.md`; `research/03_algorithmic_design/cascade_pipeline.md`.
