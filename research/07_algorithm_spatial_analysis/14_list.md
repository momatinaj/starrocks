# LIST: Learning to Index Spatio-Textual Data

## 1. Algorithm Summary

**LIST** (*Learning to Index Spatio-Textual Data*) targets **embedding-based spatial keyword queries**: objects carry both a **location** and **text embedding**, and the system must retrieve top-\(k\) results by **joint** spatial and textual relevance. Yin et al., *“LIST: learning to index spatio-textual data for embedding based spatial keyword queries”* (*The VLDB Journal*, 2025; arXiv:2403.07331—see `research/01_literature_survey/hybrid_approaches.md` and `papers/bibliography.bib`). LIST uses **learning-to-cluster** to group objects that are **co-located in space and close in embedding space**, with **pseudo-label** techniques to train without full ground-truth labels.

## 2. Mechanism Deep-Dive

**Build (high level):**

1. **Joint representation**: Combine spatial features (e.g., lat/lon or cell embedding) with text embeddings into a training signal for clustering.
2. **Learning-to-cluster**: Optimize cluster assignments so that **intra-cluster** pairs are spatially and semantically coherent (problem-specific loss; pseudo-labels from weak supervision).
3. **Index structure**: Clusters become **searchable partitions** (e.g., inverted or graph-like access per cluster—paper emphasizes learned partitions over hand-tuned grids).

**Query:**

1. Map query **location + text embedding** to likely clusters (learned routing / nearest-centroid style).
2. Search **only inside** selected clusters for ANN-style refinement.
3. Merge and rank using **combined spatial + textual** relevance models.

The survey notes **orders-of-magnitude** speedups vs. non-learned baselines for their workloads (`research/01_literature_survey/hybrid_approaches.md`).

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **ST_Contains(polygon, point)** | **CONDITIONALLY SUITABLE.** LIST is trained for **keyword + location** relevance, not arbitrary GIS predicates. **Polygon containment** can be approximated by **sampling / covering** the polygon and routing to clusters overlapping the cover, but **learned routing** may miss boundary points unless training explicitly covers polygonal filters. |
| **ST_DWithin(point, point, r)** | **CONDITIONALLY SUITABLE.** Disk queries align well with **locality-aware** clustering if \(r\) is stable in the workload; **ad-hoc radii** may reduce routing accuracy unless the router is metric-aware or uses a **hierarchical cell overlay**. |
| **ST_Intersects(geom, geom)** | **CONDITIONALLY SUITABLE.** Same caveat: general geometries need **decomposition** into spatial features the learned index sees at query time. |
| **Lat/lon range predicates** | **SUITABLE** as **rectangular filters** if the learned partitioner includes **axis-aligned splits** or if ranges are mapped to cluster IDs via a **small routing map** (e.g., range → overlapping clusters). |

**Important distinction:** LIST optimizes **spatio-textual** embedding workloads; pure **spatial + generic vector** (no text) is a **reduced** case—clustering may still help if **ρ** (spatial–vector correlation) is positive (`research/01_literature_survey/hybrid_approaches.md`, `research/03_algorithmic_design/spatial_vector_correlation.md`).

## 4. Evidence

- **Hybrid survey**: LIST reports **~3 orders of magnitude** faster than baselines for embedding-based spatial keyword queries; uses **pseudo-label generation** for weak supervision.
- **Correlation discussion** (`research/01_literature_survey/hybrid_approaches.md`): LIST targets **learned** joint structure—often aligned with **positive** spatial–embedding correlation.
- **Workload sensitivity**: Learned indexes typically need **retraining** when data/query distribution shifts—less predictable than static S2 partitions.

## 5. Proposed Spatial Optimizations

1. **Hybrid router**: Use **S2 covering** of the query region to **union** cluster candidates, then LIST routing **within** that union—reduces missed hits at boundaries.
2. **Dual loss**: Add **containment / distance** pseudo-tasks in training when deployment targets `ST_Contains` / `ST_DWithin` heavily.
3. **Fallback path**: If router confidence is low, expand to **adjacent clusters** or fall back to **IVF or HNSW** on the spatially filtered subset (`research/03_algorithmic_design/cascade_pipeline.md`).
4. **Segment-local models**: Train **per-segment** shallow routers to limit memory and simplify incremental loads (aligns with LSM-style ingestion).

## 6. Verdict

**CONDITIONALLY SUITABLE**

Justification: Strong when **workloads repeat** (similar spatial–semantic structure) and **positive ρ** holds. **Not** ideal as the **only** index for **arbitrary ad-hoc GIS predicates** without extensions (covering, refinement, or retraining). For StarRocks’ **general SQL + optional vector**, LIST is best viewed as an **optional learned partitioner**, not a replacement for exact spatial predicates.

## 7. StarRocks Fit

- **No native LIST in TenANN**: Adoption implies **offline training**, **artifact storage** (centroids, cluster maps), and **query-time routing**—a larger product surface than IVF/HNSW.
- **Segment architecture**: Fits **per-segment** cluster models **if** training volume per segment is sufficient; cold/small segments need **brute-force or IVF** fallback.
- **Planner**: Requires **selectivity + confidence** hooks to choose LIST-routed search vs. **cascade** (`research/03_algorithmic_design/cascade_pipeline.md`).
- **Text vs. generic vector**: If the deployment is **not** spatio-textual, prefer **KHI**-style attribute trees or **spatial-partitioned HNSW** (`research/03_algorithmic_design/spatial_partitioned_hnsw.md`) before LIST.

**References:** `research/01_literature_survey/hybrid_approaches.md`; `research/01_literature_survey/papers/bibliography.bib` (`list2024`); `research/03_algorithmic_design/spatial_vector_correlation.md`; `research/03_algorithmic_design/cascade_pipeline.md`.
