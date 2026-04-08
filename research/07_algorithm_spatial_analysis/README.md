# Algorithm Spatial Suitability Analysis

## Purpose

This folder systematically evaluates every algorithm referenced in the StarRocks hybrid spatial+vector research, determining its suitability for spatial queries (`ST_Contains`, `ST_DWithin`, `ST_Intersects`, and lat/lon range predicates). Each analysis proposes concrete optimizations and provides evidence-backed verdicts.

## Methodology

Each algorithm is evaluated using a standardized template with these sections:

1. **Algorithm Summary** -- one-paragraph recap of core mechanism, citing the paper
2. **Mechanism Deep-Dive** -- step-by-step build + query description
3. **Spatial Query Suitability** -- evaluation against four spatial query types:
   - `ST_Contains(polygon, point)` -- point-in-polygon containment
   - `ST_DWithin(point, point, radius)` -- distance-bounded search
   - `ST_Intersects(geometry, geometry)` -- arbitrary geometry overlap
   - Range predicates on lat/lon attributes
4. **Evidence** -- concrete numbers, complexity bounds, or qualitative findings from the original papers
5. **Proposed Spatial Optimizations** -- how the algorithm could be adapted for spatial predicates
6. **Verdict** -- SUITABLE / CONDITIONALLY SUITABLE / NOT SUITABLE with justification
7. **StarRocks Fit** -- implementation feasibility given segment architecture and TenANN

## Master Verdict Table

Abbreviations: **Cond** = conditionally suitable; **Suit** = suitable; **Ind** = indirect (needs scalarization / preprocessing). Verdicts are verbatim classes from each note (**SUITABLE** / **CONDITIONALLY SUITABLE**). Incremental: **Yes** / **No** / **Poor** / **Partial** / **—** (not an index type).

| # | Algorithm | ST_Contains | ST_DWithin | ST_Intersects | Lat/Lon Range | Incremental | Verdict | StarRocks Fit |
|---|-----------|-------------|------------|---------------|---------------|-------------|---------|---------------|
| 01 | [HNSW](01_hnsw.md) | Cond | Cond | Cond | Cond | No | CONDITIONALLY SUITABLE | High — TenANN HNSW in BE |
| 02 | [ACORN](02_acorn.md) | Cond | Cond | Cond | Cond | No | CONDITIONALLY SUITABLE | High — extend HNSW traversal |
| 03 | [Filtered-DiskANN](03_filtered_diskann.md) | Cond | Cond | Cond | Cond | No | CONDITIONALLY SUITABLE | Low — not in TenANN; large BE cost |
| 04 | [Vamana/DiskANN](04_vamana_diskann.md) | Cond | Cond | Cond | Cond | No | CONDITIONALLY SUITABLE | Low — disk ANN path; spatial external |
| 05 | [NaviX](05_navix.md) | Cond | Cond | Cond | Cond | No | CONDITIONALLY SUITABLE | Med — adaptive patterns in planner/executor |
| 06 | [Compass](06_compass.md) | Cond | Cond | Cond | Suit | — | CONDITIONALLY SUITABLE | High — cooperative OLAP + TenANN |
| 07 | [UNIFY](07_unify.md) | Cond | Cond | Cond | Suit | Partial | CONDITIONALLY SUITABLE | Med — SIG/HSIG costly; S2+HNSW lighter |
| 08 | [Dynamic Segment Graph](08_dynamic_segment_graph.md) | Cond | Cond | Cond | Suit | Yes | CONDITIONALLY SUITABLE | Low — conflicts with immutable segments |
| 09 | [WoW](09_wow.md) | Ind | Ind | Ind | Cond | Yes | CONDITIONALLY SUITABLE | Med — incremental RFANN; BE integration cost |
| 10 | [Mesh](10_mesh.md) | Suit | Suit | Cond | Suit | No | SUITABLE | Med-high — prototype-grade; high build cost |
| 11 | [KHI](11_khi.md) | Cond | Cond | Cond | Suit | Partial | CONDITIONALLY SUITABLE | Med — subgraph per leaf + zonemap synergy |
| 12 | [iRangeGraph](12_irangegraph.md) | Cond | Cond | Cond | Cond | Poor | CONDITIONALLY SUITABLE | Med — static tree; batch rebuild fits SR |
| 13 | [IVF-squared](13_ivf2.md) | Cond | Cond | Cond | Suit | No | CONDITIONALLY SUITABLE | High — IVF + inverted/bitmap fusion |
| 14 | [LIST](14_list.md) | Cond | Cond | Cond | Suit | Partial | CONDITIONALLY SUITABLE | Low — offline training; no native TenANN |
| 15 | [IVF/IVFPQ/ScaNN/SPANN](15_ivf_ivfpq_scann_spann.md) | Cond | Cond | Cond | Cond | Partial | CONDITIONALLY SUITABLE | Mixed — IVF/IVFPQ high; ScaNN/SPANN low |
| 16 | [Spatial Structures](16_spatial_structures.md) | Suit | Suit | Suit | Suit | Varies | SUITABLE | Excellent — S2; R-tree medium |

## File Listing

```
research/07_algorithm_spatial_analysis/
├── README.md                          # This file
├── 01_hnsw.md                         # Base HNSW
├── 02_acorn.md                        # ACORN (predicate-agnostic filtered HNSW)
├── 03_filtered_diskann.md             # Filtered-DiskANN (StitchedVamana + FilteredVamana)
├── 04_vamana_diskann.md               # Base Vamana / DiskANN
├── 05_navix.md                        # NaviX (native GDBMS vector index)
├── 06_compass.md                      # Compass (cooperative execution)
├── 07_unify.md                        # UNIFY (SIG / HSIG range-filtered ANN)
├── 08_dynamic_segment_graph.md        # Dynamic Segment Graph
├── 09_wow.md                          # WoW (window-to-window incremental)
├── 10_mesh.md                         # Mesh (spatial-range constrained ANN)
├── 11_khi.md                          # KHI (attribute-space partitioning + HNSW)
├── 12_irangegraph.md                  # iRangeGraph (elemental graphs + segment tree)
├── 13_ivf2.md                         # IVF-squared (classic + spatial inverted indices)
├── 14_list.md                         # LIST (learned spatio-textual index)
├── 15_ivf_ivfpq_scann_spann.md        # IVF family + ScaNN + SPANN (grouped)
├── 16_spatial_structures.md           # Classical spatial (R-tree, S2, H3, etc.)
└── comparative_analysis.md            # Head-to-head ranking, recommendation matrix
```
