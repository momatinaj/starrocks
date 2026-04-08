# IVF Family, IVFPQ, ScaNN, and SPANN (Grouped Analysis)

This note groups **coarse-partition + list scan** vector retrieval methods that share a **similar query shape**: (1) pick one or few partitions/centroids, (2) scan or score compressed postings, (3) optionally rerank. Citations and summaries draw from `research/01_literature_survey/vector_indexes.md`, `papers/bibliography.bib`, and the hybrid survey context.

---

## IVF (Inverted File)

### 1. Algorithm Summary

**IVF** partitions vectors with **k-means** (or similar) into `nlist` clusters; each cluster holds an **inverted list** of member vectors. **Jégou et al.** and the Faiss-style formulation are the common reference; StarRocks uses IVF via **TenANN** (`research/01_literature_survey/vector_indexes.md`).

### 2. Mechanism Deep-Dive

- **Build**: Train centroids; assign each vector to nearest centroid(s); store lists.
- **Query**: Find **nprobe** nearest centroids to the query; scan those lists; return top-\(k\) by exact or approximate distance.

### 3. Spatial Query Suitability

| Query | Assessment |
|-------|------------|
| **ST_Contains** | **CONDITIONALLY SUITABLE.** Partitions are **not** spatial unless centroids are trained on **location-augmented** features or **spatial strata** are used. Natural mapping: **one IVF = spatial partition** (S2 cell) per `research/01_literature_survey/vector_indexes.md`. |
| **ST_DWithin** | **CONDITIONALLY SUITABLE.** Same; small radius → few spatial partitions → small **nprobe** if aligned. |
| **ST_Intersects** | **CONDITIONALLY SUITABLE.** Needs spatial keying or post-filter. |
| **Lat/lon range** | **SUITABLE** if ranges **prune centroids** (trained with lat/lon in assignment) or **spatial side index** selects lists first. |

### 4. Evidence

- **Vector indexes survey**: IVF is **disk-friendly**; **spatial partitioning maps naturally to inverted lists**.
- **Complexity**: Scan cost \(\approx O(\sum_i |L_i|)\) over probed lists \(L_i\); list lengths depend on balance.

### 5. Proposed Spatial Optimizations

- **Spatially stratified k-means** or **separate IVF per S2 cell** (see spatial-partitioned design docs).
- **Bitmap intersection** before list scan (IVF²-style, `13_ivf2.md`).

### 6. Verdict

**CONDITIONALLY SUITABLE** (becomes **SUITABLE** when partitions are **spatially defined**).

### 7. StarRocks Fit

**High**—already integrated; planner passes **nlist/nprobe**; combines well with spatial filters and delete bitmaps.

---

## IVFPQ (IVF + Product Quantization)

### 1. Algorithm Summary

**IVFPQ** combines IVF with **product quantization (PQ)**: vectors are split into subvectors, each quantized with a **codebook**; distances are approximated via **lookup tables** (`research/01_literature_survey/vector_indexes.md`).

### 2. Mechanism Deep-Dive

- **Build**: IVF structure + PQ codes per vector.
- **Query**: Probe IVF lists; compute **asymmetric** distances (query vs. codes) fast; optional rerank on raw vectors.

### 3. Spatial Query Suitability

Same **logical** assessment as IVF: spatial predicates apply to **row IDs**; PQ affects **accuracy/speed**, not geometry. **Refinement** for exact `ST_*` should use **full coordinates**, not PQ distances.

| Query | Assessment |
|-------|------------|
| **ST_Contains / DWithin / Intersects** | **CONDITIONALLY SUITABLE** (predicate on stored geometry; PQ is orthogonal). |
| **Lat/lon range** | **SUITABLE** on scalar columns; **PQ irrelevant** unless lat/lon mistakenly quantized (not recommended for exact GIS). |

### 4. Evidence

- Survey: **memory-efficient**, **lower recall** than HNSW at similar latency.

### 5. Proposed Spatial Optimizations

- **Shortlist via IVF**, then **exact spatial check** on full-precision coords stored outside PQ.
- Keep **lat/lon or S2 ID** as **structured columns** for filtering, not PQ-coded.

### 6. Verdict

**CONDITIONALLY SUITABLE** for hybrid queries (same as IVF); **not** a spatial index by itself.

### 7. StarRocks Fit

**High**—TenANN path exists; ensure **optimizer** accounts for **recall loss** when combining aggressive PQ with strict spatial predicates (may need larger **topn** before filter).

---

## ScaNN (Scalable Nearest Neighbors)

### 1. Algorithm Summary

**ScaNN** (*Accelerating large-scale inference with anisotropic vector quantization*, Guo et al., **ICML 2020**—`scann2020` in `papers/bibliography.bib`). Uses **learned anisotropic quantization** and **asymmetric hashing** optimized for **inner-product** retrieval at scale; includes **partitioning + quantization + reranking** pipeline (`research/01_literature_survey/vector_indexes.md`).

### 2. Mechanism Deep-Dive

- **Build**: Learn quantizers that minimize IP error under a **data-dependent** metric shape (“anisotropic”).
- **Query**: Retrieve coarse candidates via **tree/partition + hashing**, score with **quantized IP**, **rerank** top candidates.

### 3. Spatial Query Suitability

| Query | Assessment |
|-------|------------|
| **ST_Contains / DWithin / Intersects** | **CONDITIONALLY SUITABLE.** ScaNN does not encode geometry; spatial constraints must be **prefilters**, **postfilters**, or **ID intersections** like generic IVF. |
| **Lat/lon range** | **CONDITIONALLY SUITABLE** as **structured** filters alongside ScaNN shortlists. |

### 4. Evidence

- Survey: **SOTA on some ANN benchmarks**; **medium** filtering adaptability vs. graph methods in the project’s qualitative table.

### 5. Proposed Spatial Optimizations

- **Cascade**: S2 bitmap / spatial index → **ScaNN on candidate IDs** (if exposed); else **post-filter** with oversampling.
- **IP vs. L2**: StarRocks distance semantics must match training (**metric consistency**).

### 6. Verdict

**CONDITIONALLY SUITABLE** for hybrid SQL—strong **vector** side, **no native spatial** semantics.

### 7. StarRocks Fit

**Low today**—**not implemented** in TenANN per survey; integration would be a **large** dependency and training pipeline. Prefer **IVF/HNSW + spatial cascade** unless IP-at-scale requirements dominate.

---

## SPANN (Space-Partition Tree + NSW)

### 1. Algorithm Summary

**SPANN** (*Chen et al., Microsoft, 2021*—`research/01_literature_survey/vector_indexes.md`): **hierarchical balanced clustering** (SPTAG-style tree) with **posting lists on SSD** and **centroids in memory**; aims at **billion-scale** data on one machine with **updates/deletes** support.

### 2. Mechanism Deep-Dive

- **Build**: Top-down partition until leaf size threshold; store **navigable small-world** links or hybrid search within leaves (paper: tree + graph flavor for fast local search).
- **Query**: Traverse tree to relevant leaves; **ANN search** within leaves; aggregate top-\(k\).

### 3. Spatial Query Suitability

| Query | Assessment |
|-------|------------|
| **ST_Contains / DWithin / Intersects** | **CONDITIONALLY SUITABLE.** Tree is **vector-driven**, not GIS-driven; spatial pruning requires **either** building the partition tree using **geo features** **or** intersecting leaf visits with **spatial covering**. |
| **Lat/lon range** | **CONDITIONALLY SUITABLE** if ranges used to **prune branches** via augmented keys (custom). |

### 4. Evidence

- Survey: **SSD-friendly**, **updates/deletes**—relevant to **ingestion-heavy** OLAP but different from StarRocks’ segment immutability model.

### 5. Proposed Spatial Optimizations

- **Geo-conditioned partitioning** at build (correlated space) to align leaves with **spatial locality** (`research/03_algorithmic_design/spatial_vector_correlation.md`).
- **Dual filtering**: S2 cover → **leaf ID set** intersection with tree navigation result.

### 6. Verdict

**CONDITIONALLY SUITABLE**—excellent for **scale-out ANN on disk**, but **spatial** is still **external** unless partitioning is **geo-aware**.

### 7. StarRocks Fit

**Medium-low**: Not in TenANN; **segment files** already provide **chunking**—overlap with SPANN’s value is **partial**. Consider **only** for **external** billion-vector stores feeding StarRocks, not core BE index.

---

## Combined Verdict Table (This Group)

| Method | Native spatial? | StarRocks today | Hybrid verdict |
|--------|-----------------|-----------------|----------------|
| IVF | No (unless trained spatially) | Implemented | CONDITIONALLY SUITABLE |
| IVFPQ | No | Implemented | CONDITIONALLY SUITABLE |
| ScaNN | No | Not implemented | CONDITIONALLY SUITABLE |
| SPANN | No | Not implemented | CONDITIONALLY SUITABLE |

**References:** `research/01_literature_survey/vector_indexes.md`; `research/01_literature_survey/hybrid_approaches.md` (IVF² fusion context); `research/03_algorithmic_design/approach_taxonomy.md`; `papers/bibliography.bib` (`scann2020`, IVF/PQ lineage).
