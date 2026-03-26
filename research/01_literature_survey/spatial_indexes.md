# Spatial Indexes

## Overview

Spatial indexes accelerate queries over geometric or geographic data. For our hybrid index, they provide the spatial filtering component. The key question is which spatial structure best integrates with vector indexes.

## Classical Spatial Index Structures

### R-tree Family

**R-tree** (Guttman, 1984)
- Balanced tree of minimum bounding rectangles (MBRs)
- Each internal node's MBR encloses all children
- Search: traverse nodes whose MBRs overlap the query region
- Insert/delete: choose subtree with minimum enlargement, split on overflow
- Complexity: O(log n) average search, O(n) worst case due to overlap

**R*-tree** (Beckmann et al., 1990)
- Improved R-tree with better split and insertion heuristics
- Minimizes overlap, coverage, and margin simultaneously
- Forced reinsertion on overflow (instead of immediate split)
- De facto standard for in-memory spatial indexing

**R+-tree** (Sellis et al., 1987)
- Eliminates overlap by allowing objects to be stored in multiple nodes
- Faster search but more expensive updates
- Good for read-heavy workloads

**Hilbert R-tree** (Kamel & Faloutsos, 1994)
- Orders objects by Hilbert curve value
- B+-tree on Hilbert values provides spatial locality
- Excellent for bulk loading and disk-based workloads

### Space-Partitioning Trees

**KD-tree** (Bentley, 1975)
- Binary tree alternating split dimensions
- Exact nearest neighbor in O(log n) average case
- Degrades badly in high dimensions (curse of dimensionality)
- Useful for 2D/3D spatial data, not for high-dimensional vectors

**Quad-tree** (Finkel & Bentley, 1974)
- Recursive 4-way partition of 2D space
- Simple to implement, good for point data
- Unbalanced for non-uniform distributions

### Grid-Based Approaches

**Geohash**
- Encodes lat/lon as base-32 string using Z-curve
- Hierarchical: prefixes define coarser cells
- Range queries map to prefix scans
- Issue: cells at different latitudes have different physical sizes

**Google S2 Geometry**
- Projects sphere onto cube faces, then Hilbert-curves each face
- Cells are approximately equal-area at each level (31 levels)
- Hierarchical cell IDs enable efficient range queries
- Cell covering: approximate any region as union of cells
- **Already used in StarRocks** (`be/src/geo/geo_types.h`)

**Uber H3**
- Hexagonal hierarchical spatial index
- Cells are hexagons (with 12 pentagons per resolution)
- More uniform neighbor distances than square grids
- 16 resolution levels, each cell has 7 children
- Popular in ride-sharing, logistics, and analytics

## Relevance to Hybrid Index

| Structure | Integration Potential | Pros | Cons |
|-----------|----------------------|------|------|
| R*-tree | High — leaf nodes can embed HNSW graphs | Natural spatial selectivity, balanced | Complex implementation, dynamic updates |
| S2 Cells | Very High — already in StarRocks | Hierarchical, equal-area, efficient covering | Fixed grid, no data-adaptive splits |
| H3 | High — popular in analytics | Hexagonal uniformity, clean API | Not in StarRocks, requires new dependency |
| KD-tree | Low — 2D only variant useful | Simple, well-understood | Not hierarchical in the useful sense |
| Geohash | Medium — simple integration | B-tree compatible (string prefixes) | Non-uniform cell sizes, edge effects |

## Recommendation

**S2 Cells** are the strongest candidate for the spatial component because:
1. Already integrated in StarRocks (`be/src/geo/`)
2. Hierarchical cell IDs support multi-resolution queries
3. Cell covering algorithm approximates arbitrary polygons
4. Equal-area cells provide uniform spatial partitioning for vector sub-indexes
5. Integer cell IDs are efficient for bitmap/range operations

## Open Questions

- What S2 level provides the best granularity for partitioning? (Level 12 ≈ 3.3 km² cells, Level 15 ≈ 0.05 km² cells)
- Should the partition granularity be adaptive based on data density?
- How to handle queries that span multiple S2 cells efficiently?
