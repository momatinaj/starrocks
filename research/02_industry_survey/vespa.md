# Vespa

## Overview

Vespa is Yahoo's open-source search platform supporting ANN search via HNSW and geo filtering via `geoLocation` queries. Combined queries use AND in YQL.

## Geo Support

**Field type**: `position` — latitude/longitude coordinates  
**Query clause**: `geoLocation(field, lat, lon, "radius")` in YQL  
**Distance ranking**: `closeness(field)` and `distance(field)` rank features

## Vector Support

**Index**: HNSW with configurable parameters  
**Query operator**: `nearestNeighbor(embedding_field, query_vector_name)`  
**Parameters**: `targetNumHits` controls candidates, `hnsw.exploreAdditionalHits` for recall tuning

## Combined Query

```yql
SELECT * FROM sources
WHERE {targetNumHits: 10000}nearestNeighbor(embedding, q)
AND geoLocation(locations, 11, -110, "100 km")
```

## Pre-filter vs Post-filter Issue

**Historical problem**: `geoLocation` had `allow_termwise_eval: 0`, meaning it was applied as a post-filter after ANN results were retrieved. This meant:
1. ANN search returns `targetNumHits` candidates ignoring location
2. Geo filter removes non-matching candidates
3. Result set smaller than expected

**Fix** (GitHub issue #22075): Changed to allow pre-filtering with `geoLocation`, so the spatial constraint restricts the HNSW traversal candidates.

## Strengths

- Mature platform with production scale (Yahoo, Spotify)
- YQL provides clean syntax for combined queries
- HNSW with global filtering mechanism
- Distance-based ranking features for geo-aware scoring

## Limitations

- Pre-filtering for geo is a recent addition (was post-filter only)
- No spatial partitioning of the HNSW graph
- `position` type is point-only (no polygons or complex geometries)
- Geo filter is treated as generic filter, not spatial-structure-aware

## Relevance to StarRocks

- **Pre-filter vs post-filter lesson**: Pre-filtering is essential for spatial queries; post-filtering wastes ANN search budget on geographically irrelevant results
- **YQL syntax** for combined queries is a good model for StarRocks SQL syntax design
- **Limitation**: Even after the fix, Vespa's approach is generic filtering, not spatially-optimized HNSW traversal
