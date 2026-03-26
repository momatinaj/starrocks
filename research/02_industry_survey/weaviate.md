# Weaviate

## Overview

Weaviate is a vector database with built-in ML modules and GraphQL API. It supports geo filtering via `WithinGeoRange` combined with vector search operators.

## Geo Support

**Data type**: `geoCoordinates` property with `latitude` and `longitude`  
**Filter operator**: `WithinGeoRange` — filters objects within a specified distance from a point

**Limitation**: Only supports point+radius queries, no polygon containment or complex spatial predicates.

## Vector Support

**Search operators**:
- `nearVector`: Search by raw vector embedding
- `nearText`: Search by text (auto-embedded via ML module)
- `nearImage`: Search by image embedding

**Index**: HNSW-based

## Combined Query

```graphql
{
  Get {
    Signs(
      nearVector: { vector: [0.1, 0.2, ...] }
      where: {
        operator: WithinGeoRange
        valueGeoRange: {
          geoCoordinates: { latitude: 37.77, longitude: -122.42 }
          distance: { max: 5000 }
        }
        path: ["location"]
      }
      limit: 10
    ) {
      name
      _additional { distance }
    }
  }
}
```

## Architecture

- Geo filter is applied as a `where` clause on vector search results
- Filter evaluation order is managed by the query engine
- No explicit documentation on pre-filter vs post-filter optimization for geo
- HNSW graph is not spatially partitioned or geo-aware

## Strengths

- Clean GraphQL API for combined queries
- Built-in ML modules for automatic embedding
- Simple geo coordinate support

## Limitations

- Only `WithinGeoRange` (point+radius) — no polygon, bbox, or complex spatial predicates
- No spatial indexing of coordinates (likely sequential evaluation)
- Sparse documentation on how geo filtering interacts with HNSW traversal
- No spatial hierarchy or cell-based partitioning

## Relevance to StarRocks

- **Minimal relevance** — Weaviate's geo support is too basic for our use case
- **API design** is clean but the underlying implementation lacks spatial optimization
- Validates market demand for geo+vector search even with limited capabilities
