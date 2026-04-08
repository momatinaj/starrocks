# Filtered-DiskANN — Spatial Query Suitability

## 1. Algorithm Summary

**Filtered-DiskANN** (Gollapudi et al., *Filtered-DiskANN: Graph Algorithms for Approximate Nearest Neighbor Search with Filters*, ACM **The Web Conference (WWW)**, 2023) extends **DiskANN / Vamana** so that **label** (filter) metadata influences **graph construction**, not only query-time filtering. Two constructions are central: **StitchedVamana** (per-label graphs stitched together) and **FilteredVamana** (single graph with **filter-aware** pruning). Goal: preserve **connectivity within label classes** for SSD-resident ANN under filters. See [filtered_ann.md](../01_literature_survey/filtered_ann.md), [vector_indexes.md](../01_literature_survey/vector_indexes.md), and [bibliography.bib](../01_literature_survey/papers/bibliography.bib) (`filtered_diskann2023`).

## 2. Mechanism Deep-Dive

**Shared base (Vamana-style)**

- Points live on disk; in-memory index holds graph + compressed vectors/pointers as in DiskANN.

**StitchedVamana**

1. Build a **separate** Vamana graph for each **label** (or label combination, depending on granularity).
2. **Stitch** graphs by adding cross-label edges so navigation can move between label-induced subgraphs when queries allow multiple labels.

**FilteredVamana**

1. Build **one** graph; during **RobustPrune** (or equivalent), bias neighbor retention so that nodes remain well-connected **within** their label classes — filter-aware pruning reduces dead ends when search is restricted to a label.

**Query**

- Beam search on the graph with **filter-aware** neighbor exploration (details in paper); leverages disk-friendly sequential access patterns of DiskANN.

## 3. Spatial Query Suitability

| Query type | Assessment |
|------------|--------------|
| **`ST_Contains(polygon, point)`** | Spatial regions are **not** a single static label unless you **discretize** space (e.g., S2 cell id as label). Arbitrary polygons do not map to one label — you need **multiple cells**, **overlapping label sets**, or **query-time** combination; **StitchedVamana** “per region” does not scale to infinite polygons. **Practical use**: **coarse cells** or **partition ids** as labels + exact geometry check at the end. |
| **`ST_DWithin(..., r)`** | Same: unless r aligns with a fixed spatial quantization, predicate is **not** a single label. **Buffer** queries often need **many** cells or a **non-graph** spatial phase. |
| **`ST_Intersects(geometry, geometry)`** | Generally **ill-suited** as a single-label filter; use **spatial index** or **cell prefilter**, then Filtered-DiskANN on the **reduced label set** if labels encode cells. |
| **Lat/lon range predicates** | **Best match** among spatial predicates: ranges can define **rectangular** label buckets or **sorted attribute** segments (conceptually similar to range-filtered ANN lines of work cited in [filtered_ann.md](../01_literature_survey/filtered_ann.md)). Still requires **label design** aligned to query patterns. |

**Summary**: Filtered-DiskANN fits **discrete, build-time-known** filter dimensions. **Continuous** or **ad-hoc** spatial predicates require **mapping** to labels (cells, partitions) or **hybrid** execution ([hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md)).

## 4. Evidence

- **Claimed benefit**: Graph connectivity **within** filter classes vs. similarity-only Vamana ([filtered_ann.md](../01_literature_survey/filtered_ann.md), [vector_indexes.md](../01_literature_survey/vector_indexes.md)).
- **StarRocks mapping**: “If S2 cell IDs are treated as labels, filter-aware construction ensures connectivity within spatial regions”; **StitchedVamana** parallels “per-spatial-partition graphs, stitch across partitions” ([filtered_ann.md](../01_literature_survey/filtered_ann.md)).
- **Trade-off**: [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md) notes **Filtered HNSW (Filtered-DiskANN-style)** has **build-time** commitment to label structure → **less** query-time flexibility than pure ACORN on standard HNSW.

## 5. Proposed Spatial Optimizations

1. **S2/H3 labels at fixed level**: Use cell id as **primary label** for FilteredVamana/StitchedVamana; query supplies a **set of cell ids** intersecting the spatial predicate, then run filtered ANN over the union ([filtered_ann.md](../01_literature_survey/filtered_ann.md)).
2. **Segment-aligned stitching**: Align with StarRocks **segments**: local graphs per heavy-hitting spatial shard, **stitch** at boundaries for cross-cell queries ([filtered_ann.md](../01_literature_survey/filtered_ann.md), [hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md)).
3. **Negative ρ**: When partition boundaries cut true k-NN in vector space, increase **cross-partition** stitch edges or `efSearch` — consistent with correlation discussion in [hybrid_approaches.md](../01_literature_survey/hybrid_approaches.md).
4. **Hybrid with cascade**: For arbitrary polygons, use **spatial index → candidate cell ids →** Filtered-DiskANN on those labels (dual-index mindset per [approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md)).

## 6. Verdict

**CONDITIONALLY SUITABLE**

Strong when spatial filtering can be **reduced to a manageable set of discrete labels** (cells, buckets, segment ids) and **disk** residency matters. **Not** a drop-in for arbitrary **OGC geometry** without preprocessing. Complements — rather than replaces — **R-tree / S2 filter** stages for complex shapes.

## 7. StarRocks Fit

- **Not** in TenANN today ([vector_indexes.md](../01_literature_survey/vector_indexes.md)); **non-trivial** BE investment (disk graph IO, construction pipelines).
- **Where it could shine**: **Billion-scale** per-table vector cold tiers with **coarse** spatial partitioning; **stitching** matches **multi-segment** serving if labels align with storage layout ([filtered_ann.md](../01_literature_survey/filtered_ann.md)).
- **Planner**: Expose **label granularity** and **stitch** cost in the optimizer; wrong label schema → wasted build cost ([approach_taxonomy.md](../03_algorithmic_design/approach_taxonomy.md)).
