# Qdrant

## Overview

Qdrant is a Rust-based vector database with the most sophisticated filtered search architecture among current vector databases. It uses a "Filterable HNSW" approach that builds filter-aware graph connections.

## Geo Support

**Payload type**: `geo` — stores `{lon, lat}` coordinates as payload metadata.

**Geo filter operations**:
- `geo_radius`: Points within distance from center point
- `geo_bounding_box`: Points within rectangular bounds (handles anti-meridian)
- `geo_polygon`: Points within polygon (supports interior holes)

**Payload indexing**: Geo fields must be explicitly indexed. Qdrant does NOT auto-index payload fields. Best practice is to create payload indexes before uploading data so the HNSW index builds filter-aware links.

## Architecture: Filterable HNSW

Qdrant's key innovation is building filter awareness into the HNSW graph itself:

1. **Index build time**: When payload indexes exist before data upload, HNSW construction considers payload values when creating graph edges. It builds subgraphs per payload value and merges them.

2. **Query time**: A query planner evaluates filter cardinality and selects strategy:
   - **Restrictive filters** (few matches): Fall back to full scan on filtered subset
   - **Permissive filters** (many matches): HNSW traversal, skip non-matching nodes

3. **Subgraph approach**: For each payload value, Qdrant maintains connectivity within the subgraph of matching nodes. This ensures the HNSW graph remains navigable even when many nodes are filtered out.

## Geo+Vector Query Example

```json
{
  "vector": [0.1, 0.2, ...],
  "filter": {
    "must": [
      {
        "geo_radius": {
          "key": "location",
          "center": { "lat": 37.7749, "lon": -122.4194 },
          "radius": 5000
        }
      }
    ]
  },
  "limit": 10
}
```

## Strengths

- Filter-aware HNSW construction ensures graph connectivity for common filter values
- Query planner adapts strategy based on filter selectivity
- Rich geo filter types (radius, bbox, polygon with holes)
- Explicit payload indexing gives user control over what's optimized

## Limitations

- Subgraph approach is per-payload-value, not spatial-hierarchy-aware
- No S2/H3 cell-based spatial optimization
- Filter-aware construction only works when payload indexes exist before data load
- Geo coordinates are flat metadata, not leveraged for spatial partitioning
- No spatial join or complex spatial operations (ST_Contains, ST_Intersects on arbitrary geometries)

## Relevance to StarRocks

- **Filterable HNSW concept** is directly applicable — we should build spatial awareness into the HNSW graph
- **Query planner selectivity estimation** is a pattern to replicate in StarRocks' FE optimizer
- **Limitation**: Qdrant's approach doesn't exploit spatial hierarchy (S2 cells), which is an opportunity for our design
