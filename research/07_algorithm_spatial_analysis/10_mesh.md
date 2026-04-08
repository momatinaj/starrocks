# Mesh (Spatial-Range-Constrained ANN on Geo-Tagged Vectors)

## 1. Algorithm Summary

**Mesh** targets **k-RANNS**: top-k approximate nearest neighbor search among **geo-tagged high-dimensional vectors** subject to a **spatial range** constraint (region in geographic space). Song et al., *Efficient top-k spatial-range-constrained approximate nearest neighbor search on geo-tagged high-dimensional vectors*, **The VLDB Journal** 34(14), Jan 2025 (DOI 10.1007/s00778-024-00894-5; see `mesh2025` in [`bibliography.bib`](../01_literature_survey/papers/bibliography.bib)). The index is **memory-efficient** and **workload-aware**: index construction is cast as a **combinatorial optimization** (NP-hard) with a **theoretical approximation**, and **query execution adapts** to estimated **spatial selectivity**. See [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md) and [`ITERATION_LOG.md`](../ITERATION_LOG.md) for the research team’s assessment that Mesh is among the **most directly relevant** academic references for hybrid geo+vector search in StarRocks.

## 2. Mechanism Deep-Dive

**Index construction**

1. Input: points with **coordinates** (e.g., lat/lon) and **embedding vectors**.
2. The method **partitions** or **groups** data to balance **memory footprint** vs. **expected query performance** across spatial range selectivities (workload-aware objective).
3. An **approximation algorithm** solves (or bounds) the grouping problem — avoiding naive full materialization of every possible region.

**Query processing**

1. Given **query vector q**, **k**, and **spatial region R** (range in geographic space):
2. Estimate **selectivity** |R ∩ data| / |data| (or use workload statistics).
3. Choose an **adaptive strategy** — e.g., emphasize **spatial pruning** when the region is selective vs. **vector-heavy** search when the region is large — matching the paper’s adaptive query algorithms.
4. Return **approximate** top-k by vector distance among points **inside R**.

The design sits in the **unified structure** branch of the taxonomy ([`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md)): **joint** spatial+vector structure rather than purely predicate-agnostic filtered HNSW.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|------------|
| **`ST_Contains(polygon, point)`** | **Well aligned.** Region containment is exactly the **spatial half** of k-RANNS when the query region is the polygon’s interior (or a conservative approximation). Exact predicates may require **point-in-polygon** verification on candidates. |
| **`ST_DWithin(point, point, r)`** | **Well aligned.** A **metric disk** is a standard spatial range; engines typically use **bounding filters** + exact distance check. Mesh’s spatial-range semantics match **disk / cap** queries on the sphere with usual geodesic or projected-distance implementations. |
| **`ST_Intersects(geom, geom)`** | **Conditionally aligned.** **Rectangles**, **polygons**, and **line buffers** can be handled as **query regions**; **complex** or **multi-polygon** shapes increase **approximation** cost (covering cells, multiple sub-ranges). Fully general **geometry–geometry** intersection may need **decomposition** into region lists. |
| **Lat/lon range predicates** | **Native fit.** Axis-aligned **bounding boxes** in lat/lon are the simplest spatial ranges and map cleanly to Mesh’s **selectivity-aware** pruning. |

## 4. Evidence

- **Problem match:** [`hybrid_approaches.md`](../01_literature_survey/hybrid_approaches.md) states Mesh addresses **k-RANNS** on **geo-tagged vectors** with **workload-aware** construction and **selectivity-adaptive** queries — directly overlapping StarRocks’ hybrid GIS+ANN scenario ([`research/README.md`](../README.md)).
- **Optimization stance:** Same source notes a **theoretically guaranteed approximation** for the **NP-hard** grouping problem — relevant when tuning **S2 level** or partition count under memory caps.
- **Architecture kinship:** [`spatial_partitioned_hnsw.md`](../03_algorithmic_design/spatial_partitioned_hnsw.md) cites Mesh alongside **KHI** as related work for **spatial partitioning + per-partition vector indexes**, differing in **workload-optimal** construction vs. manual S2 level choice.
- **Taxonomy:** [`approach_taxonomy.md`](../03_algorithmic_design/approach_taxonomy.md) lists Mesh under **workload-adaptive** learning of spatial distribution.

## 5. Proposed Spatial Optimizations

1. **S2 / H3 binding:** Implement Mesh-style **grouping** over **S2 cells** (already in StarRocks) so spatial ranges become **cell cover** queries with known statistics per cell.
2. **Correlation-aware build:** Feed **ρ̂** (spatial–vector correlation) from [`spatial_vector_correlation.md`](../03_algorithmic_design/spatial_vector_correlation.md) into Mesh’s workload model — high **ρ** favors **tighter** spatial grouping; **ρ ≈ 0** favors **predicate-agnostic** filtered traversal (ACORN) inside each group.
3. **Polygon pipeline:** Use **S2 polygon cover** + **Mesh** vector search on covered cells, then **PIR** exact `ST_Contains` on top-k′ candidates.
4. **Planner integration:** Export **selectivity estimates** from **column stats** + **histograms** on spatial keys to drive Mesh’s **adaptive query** branch (same conceptual lever as [`filtered_ann.md`](../01_literature_survey/filtered_ann.md) selectivity table).

## 6. Verdict

**SUITABLE**

Mesh is one of the few surveyed algorithms **explicitly designed** for **geo-tagged vectors** under **spatial range** constraints with **memory-bounded**, **selectivity-aware** execution. Remaining work is **engineering** (integration with StarRocks segments, spherical predicates, and cost model), not a fundamental **semantic mismatch**.

## 7. StarRocks Fit

- **Segment architecture:** Mesh’s **grouping** maps naturally to **per-segment** or **per-tablet** spatial partitions with **embedded** TenANN graphs — consistent with **Grid-HNSW** direction in [`research/README.md`](../README.md), upgraded with **workload-optimal** partition boundaries instead of fixed S2 level only.
- **TenANN:** Per-partition **HNSW** (or Vamana) slots under Mesh’s vector side; spatial side uses **cell lists** or **R-tree**-style catalogs as in design sketches.
- **Implementation cost:** High — requires **combinatorial optimizer** for grouping and **runtime adaptive** strategy; likely **research prototype** before production hardening.
- **Incremental:** Mesh emphasizes **memory-efficient static / batch** structure in the journal framing; **incremental** loads may prefer **WoW** or **Dynamic Segment Graph** for streaming inserts ([`filtered_ann.md`](../01_literature_survey/filtered_ann.md)), then **periodic** Mesh rebuild.
