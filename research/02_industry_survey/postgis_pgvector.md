# PostGIS + pgvector

## Overview

PostGIS and pgvector are PostgreSQL extensions that provide spatial and vector search capabilities respectively. Combined in the same database, they enable SQL queries that mix spatial predicates with vector similarity — the closest existing solution to our target use case.

## Spatial Support (PostGIS)

**Data types**: `geometry`, `geography` (with SRID)  
**Index**: GiST (Generalized Search Tree) for spatial operations  
**Functions**: Full OGC SFS compliance — `ST_Contains`, `ST_Within`, `ST_Intersects`, `ST_Distance`, `ST_Buffer`, `ST_DWithin`, etc.  
**Capabilities**: 2D/3D/4D, all geometry types, coordinate reference systems, spatial joins

## Vector Support (pgvector)

**Data type**: `vector(n)` — fixed-dimension float vector  
**Indexes**: HNSW (v0.5.0+), IVFFlat  
**Distance operators**: `<->` (L2), `<=>` (cosine), `<#>` (inner product)  
**Search**: ORDER BY distance operator + LIMIT k

## Combined Query

```sql
SELECT s.name,
       s.embedding <-> '[0.1, 0.2, ...]'::vector AS dist
FROM signs s
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((...))'),
    s.location
)
ORDER BY dist
LIMIT 10;
```

## Architecture: Separate Index Paths

```
Query Planner
├── GiST Index Scan (spatial filter on 'location')
│   └── Returns row IDs matching spatial predicate
├── HNSW Index Scan (vector similarity on 'embedding')
│   └── Returns row IDs ordered by vector distance
└── Bitmap AND / Nested Loop
    └── Combines results from both index paths
```

**Critical point**: The query planner decides whether to:
1. Use GiST first, then brute-force vector distance on survivors
2. Use HNSW first (with oversampling), then filter by spatial predicate
3. Sequential scan with both predicates evaluated per row

The planner uses cost estimates, but there is no coordinated index traversal — the two indexes are completely independent.

## Practical Considerations

**Extension stacking**: Multiple PostgreSQL extensions (PostGIS, pgvector, TimescaleDB) can coexist, sharing the same query planner and executor.

**ORM challenges**: ORMs like MikroORM don't natively support extension-specific operators. Developers use raw SQL or hybrid approaches (e.g., Kysely for spatial+vector queries, ORM for CRUD).

**Real-world use cases**:
- Location-based marketplaces: spatial delivery zones + semantic product discovery
- Smart city: sensor locations + vector-based event classification
- Fleet management: real-time location tracking + similarity search on telemetry

## Strengths

- Full SQL interface with rich spatial and vector capabilities
- Mature, well-tested spatial implementation (PostGIS has decades of production use)
- Query planner can optimize access paths
- Same database — no data movement between spatial and vector systems
- Extensible PostgreSQL ecosystem

## Limitations

- **No unified index**: GiST and HNSW are separate structures with no coordination
- **Query planner limitations**: PostgreSQL's cost model may not accurately estimate the selectivity of combined spatial+vector queries
- **No filter-aware HNSW**: pgvector's HNSW doesn't support predicate-aware traversal
- **Row-store overhead**: PostgreSQL is a row-store; large-scale analytical workloads are slower than columnar systems
- **No spatial partitioning of vector index**: All vectors in the same HNSW graph regardless of location

## Relevance to StarRocks

- **SQL syntax model**: The query pattern (`WHERE ST_Contains(...) ORDER BY distance LIMIT k`) is exactly our target
- **Dual-index approach** is the baseline to beat — we want a single hybrid index that outperforms separate GiST + HNSW
- **PostGIS's spatial function set** defines the spatial capabilities we should support
- **Lesson**: Even with both capabilities in the same database, the lack of index-level integration is a fundamental limitation
- **Opportunity**: StarRocks' columnar engine + per-segment indexes can potentially achieve what PostgreSQL's row-store + separate indexes cannot
