# KHI (HNSW with Attribute-Space Partitioning)

## 1. Algorithm Summary

**KHI** addresses **multi-attribute range filtered** approximate nearest neighbor search (RFANNS): each object has a high-dimensional **vector** and **numeric attributes**, and queries specify **conjunctive range predicates** on those attributes plus **k-NN** on the vector. Yu et al., *Efficient Approximate Nearest Neighbor Search under Multi-Attribute Range Filter*, **PVLDB** 18 (2024/2025; see [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md)). The index combines an **attribute-space partitioning tree** (KD-tree-like splits with a **skew-aware** rule) with **HNSW graphs** stored at tree nodes. [`ITERATION_LOG.md`](../ITERATION_LOG.md) cites up to **16.22×** throughput improvement on reported benchmarks vs. baselines. Bibliography cross-reference: align with survey entries in [`research/01_literature_survey/papers/bibliography.bib`](../01_literature_survey/papers/bibliography.bib) (KHI discussed in narrative sources; Mesh/UNIFY entries illustrate adjacent RFANN cluster).

## 2. Mechanism Deep-Dive

**Index construction**

1. Treat structured attributes as coordinates in a **filtering space** (orthogonal to embedding space).
2. Recursively **split** nodes so partitions stay **balanced** and **height** stays bounded (skew-aware splitting).
3. For each partition region, **build HNSW** on vectors whose attributes fall in that region.

**Query processing**

1. Given query vector **q**, **k**, and **range constraints** on attributes (hyper-rectangle in attribute space):
2. **Identify** all tree nodes whose **bounding regions intersect** the query hyper-rectangle.
3. Run **HNSW search** within each relevant subgraph; **merge** candidates to obtain global top-k approximations.

[`spatial_vector_correlation.md`](../03_algorithmic_design/spatial_vector_correlation.md) highlights KHI when **vectors and attributes (including spatial bins)** **jointly** constrain search — i.e., spatial dimensions participate as **first-class filter axes**.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **Conditionally suitable.** Represent the polygon’s **footprint** via **covering cells**, **multiple rectangles**, or a **loose MBR**; query intersecting partitions then **verify** containment. **Tight** shapes need **refinement** or **many** partition hits. |
| **`ST_DWithin(point, point, r)`** | **Conditionally suitable.** A **cap / disk** is not a single **axis-aligned** box in lat/lon; typical pattern is **conservative bounding box** + **Haversine** filter, or **multiple** boxes along the circle. |
| **`ST_Intersects(geom, geom)`** | **Conditionally suitable** for **region intersection** when reduced to **bbox / cell set** intersection against indexed partitions; **exact** intersection often **post-filters** ANN candidates. |
| **Lat/lon range predicates** | **Strong fit.** **Lon** and **lat** intervals define a **2D orthotope** — a direct instance of **multi-attribute range RFANNS** KHI is built for. |

## 4. Evidence

- **Architecture:** [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md) — attribute-space tree + **HNSW at nodes**, skew-aware splits, partition intersection + aggregation.
- **Throughput claim:** [`ITERATION_LOG.md`](../ITERATION_LOG.md) — **16.22×** improvement (per paper’s experimental section).
- **Taxonomy:** [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md) classifies KHI under **unified structure** (joint spatial/attribute + vector) vs. pure graph-modification filtered ANN.
- **Design generalization:** [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md) — KHI **generalizes** fixed S2 partitioning to **arbitrary attribute dimensions** while keeping **per-partition HNSW**.

## 5. Proposed Spatial Optimizations

1. **Prioritize spatial splits** when **spatial selectivity** is high (see **σ_s** and planner rules in [`spatial_vector_correlation.md`](../03_algorithmic_design/spatial_vector_correlation.md)).
2. **S2 cell id** as an attribute to align tree regions with **spherical** proximity and reduce **thin diagonal** boxes in raw lon/lat.
3. **Stitching** adjacent leaf graphs when **ρ** is negative or boundaries are dense (StitchedVamana-style; [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md)).
4. **Hybrid plan:** Combine **KHI partition pruning** with **ACORN**-style traversal inside large leaves when **filter pass rate** is high ([`filtered_ann.md`](../01_literature_survey/filtered_ann.md) selectivity table).

## 6. Verdict

**CONDITIONALLY SUITABLE**

KHI is an excellent structural match for **lat/lon box** (and multi-dimensional **numeric**) filters combined with ANN. **Arbitrary polygons** and **geodesic disks** require **approximation layers** or **post-filtering**, so it is not a complete GIS engine by itself — but it **is** well aligned with how **OLAP** systems often **prune** with MBRs and **refine** with exact predicates.

## 7. StarRocks Fit

- **ZoneMaps / zone indexes:** Attribute min/max metadata already supports **partition elimination**; KHI adds **vector connectivity** per zone.
- **TenANN:** Natural mapping is **one HNSW subgraph per leaf region**; queries touch **few** subgraphs when filters are selective.
- **Compaction:** Tree **rebalancing** on bulk load is simpler than **fine-grained** dynamic splits in streaming ingestion; pair with **WoW** / **Dynamic Segment Graph** literature for **incremental** attribute streams ([`filtered_ann.md`](../01_literature_survey/filtered_ann.md)).
- **vs. Mesh:** Mesh optimizes **geo-native** k-RANNS and **workload-aware** grouping; KHI wins when **many non-spatial** numeric filters **correlate** with vector search — choose per **workload mix** ([`approach_taxonomy.md`](../03_algorithmic_design/approach_taxonomy.md)).
