# Vamana / DiskANN — Spatial Query Suitability

## 1. Algorithm Summary

**DiskANN** (Subramanya et al., *DiskANN: Fast Accurate Billion-point Nearest Neighbor Search on a Single Node*, **NeurIPS**, 2019) implements **Vamana**: a **single-layer** proximity graph on high-dimensional points with **bounded degree** via the **RobustPrune** procedure, engineered so search streams efficiently from **SSD** with high recall. The graph is built from **vector similarity only**; there is no native notion of geography or structured filters in the base algorithm. See [vector_indexes.md](../01_literature_survey/vector_indexes.md) and [bibliography.bib](../01_literature_survey/papers/bibliography.bib) (`diskann2019`).

## 2. Mechanism Deep-Dive

**Build (Vamana)**

1. Process vertices in a **permutation order** (often random or data-dependent).
2. For each point, add outgoing edges to a candidate set, then **RobustPrune** to limit out-degree while preserving **navigability** (short paths to true nearest neighbors in the graph).
3. Aim for **low diameter** + **bounded degree** → predictable IO per hop for disk resident indexes.

**Query**

1. **Greedy** graph traversal from a start node with a **beam** (frontier) of candidates.
2. Fetch neighbor lists and vectors (often **PQ-compressed**) from disk in a **sequential-friendly** pattern.
3. Return approximate k-NN.

**Spatial**: absent unless extended (e.g., **Filtered-DiskANN** in [03_filtered_diskann.md](03_filtered_diskann.md)).

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|--------------|
| **`ST_Contains(polygon, point)`** | Base DiskANN: only via **post-filtering** k-ANN results (recall risk) or **external** spatial filtering to a candidate id set before/during search. **No** geometric routing in the graph. |
| **`ST_DWithin(..., r)`** | Same: **metric** is embedding distance in search, **geodesic/Euclidean** distance in predicate — independent unless fused by design. |
| **`ST_Intersects(geometry, geometry)`** | **No** structural support; requires **dual-index** or **prefilter** pipeline ([approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md)). |
| **Lat/lon range predicates** | Can **pre**-select row ids satisfying range (B-tree, zone map, column scan) then **restrict** DiskANN search if the engine supports id-filtered ANN; the **Vamana graph itself** does not index lat/lon. |

**Disk strengths / spatial mismatch**: Optimized for **billion-point** k-NN on one node; spatial workloads often need **small working sets** in geographic regions — **IVF-like** or **partitioned** layouts sometimes match that better ([vector_indexes.md](../01_literature_survey/vector_indexes.md)).

## 4. Evidence

- **Scale & IO**: Designed for **single-node billion-point** ANN with **SSD** efficiency ([vector_indexes.md](../01_literature_survey/vector_indexes.md)).
- **Filtering**: Base paper is **not** filtered-ANN; **Filtered-DiskANN** (WWW 2023) is the documented extension for **label** filters ([filtered_ann.md](../01_literature_survey/filtered_ann.md), [vector_indexes.md](../01_literature_survey/vector_indexes.md)).
- **Comparison table** (project synthesis): DiskANN/Vamana marked **“Very High”** disk friendliness, **“High”** filtering adaptability **with** Filtered-DiskANN ([vector_indexes.md](../01_literature_survey/vector_indexes.md)).

## 5. Proposed Spatial Optimizations

1. **Adopt Filtered-DiskANN** when labels = spatial buckets (S2 cells, segment ids) — see [03_filtered_diskann.md](03_filtered_diskann.md).
2. **Cascade**: Spatial index (R-tree, S2) → candidate set → **Vamana search restricted to candidates** (engine support required) — **Baseline / R-tree + HNSW** pattern in [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md) applies conceptually to disk graphs.
3. **Geographic partitioning**: Build **separate** Vamana indexes per spatial partition; query touches **few** partitions — reduces disk working set ([hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md)).
4. **Do not rely** on vector graph for **ST_* semantics**; keep **exact** geometry verification on the **result** or **candidate** set per OLAP correctness expectations.

## 6. Verdict

**CONDITIONALLY SUITABLE**

- **SUITABLE** as a **large-scale, disk-resident pure k-ANN** backend when spatial constraints are handled **outside** the graph (prefilter, post-filter, or **Filtered-DiskANN** labels).
- **NOT SUITABLE** as a **standalone** answer to arbitrary **OGC spatial** predicates — same fundamental gap as HNSW: **similarity graph ≠ spatial index**.

## 7. StarRocks Fit

- **Current status**: **Not** listed as implemented in TenANN ([vector_indexes.md](../01_literature_survey/vector_indexes.md)); HNSW/IVF are the practical baselines.
- **Feasibility**: High engineering cost (disk IO path, memory mapping, build tooling). Value is clearest for **very large** cold vector columns where **memory HNSW** is prohibitive.
- **Alignment**: Columnar **segments** + **zone maps** could **prefilter** row ids before ANN; integrating **Vamana** would mirror **“spatial partition then vector index”** in [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md) at **storage** scale — evaluate **only** if product requirements demand **single-node billion-row** vector search with **spatial** side conditions.
