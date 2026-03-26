# Pinecone

## Overview

Pinecone is a managed vector database service. It supports metadata filtering on vector search but has no geo/spatial capabilities.

## Geo Support

**Not supported.** Geo-distance metadata filtering is a community feature request (not implemented as of 2025).

Users must encode location as scalar metadata and use range filters as workarounds:
```python
index.query(
    vector=[0.1, 0.2, ...],
    top_k=10,
    filter={
        "lat": {"$gte": 37.0, "$lte": 38.0},
        "lon": {"$gte": -123.0, "$lte": -122.0}
    }
)
```

## Metadata Filtering

**Syntax**: MongoDB-style query operators (`$eq`, `$ne`, `$gt`, `$gte`, `$lt`, `$lte`, `$in`, `$nin`)  
**Types**: String, number, boolean, list of strings  
**Combination**: Implicit AND on multiple fields; `$or` for disjunction

## Architecture

- Metadata filters are applied during ANN search
- "The Missing WHERE Clause" article describes the pre-filter vs post-filter trade-off
- Internal implementation details not public (managed service)

## Strengths

- Simple metadata filtering API
- Managed service eliminates operational overhead
- Serverless tier for small workloads

## Limitations

- No geo/spatial support
- Metadata types limited (no geometry, no spatial operators)
- Closed-source — cannot extend for custom index types
- No SQL interface

## Relevance to StarRocks

- **Minimal relevance** — Pinecone lacks spatial capabilities
- **Filter API design** (MongoDB-style) is a common pattern but not directly applicable to SQL syntax
- Validates that geo filtering is a gap in the vector DB market
