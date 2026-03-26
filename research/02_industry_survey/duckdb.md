# DuckDB

## Overview

DuckDB is an embedded OLAP database with separate spatial and VSS (Vector Similarity Search) extensions. As of 2025, these extensions do not integrate with each other.

## Spatial Support

**Extension**: `spatial`  
**Types**: Geometry types via GEOS/GDAL  
**Functions**: ST_Contains, ST_Distance, ST_Intersects, etc.  
**Index**: R-tree (experimental)

## Vector Support

**Extension**: `vss` (Vector Similarity Search)  
**Type**: `FLOAT[n]` (fixed-size ARRAY)  
**Index**: HNSW  
**Distance functions**: `array_distance` (L2), `array_cosine_distance`, `array_negative_inner_product`  
**Query pattern**: `ORDER BY array_distance(col, constant) LIMIT k`

**Recent improvements (Oct 2024)**:
- Faster HNSW index creation (better multi-threaded work distribution)
- Expression normalization (`1 - array_cosine_similarity` → `array_cosine_distance`)
- Progress bar for index creation

## Combined Query

```sql
SELECT *, array_distance(embedding, [0.1, 0.2, ...]::FLOAT[128]) AS dist
FROM signs
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((...))'),
    location
)
ORDER BY dist
LIMIT 10;
```

**Note**: This query would NOT use the HNSW index because the spatial predicate prevents the optimizer from applying the ANN index scan. The HNSW index only activates for simple `ORDER BY distance LIMIT k` patterns without additional predicates.

## Architecture

- Spatial and VSS are completely separate extensions
- No combined index or coordinated query plan
- HNSW index requires specific query pattern (ORDER BY + LIMIT without additional filters)
- Spatial queries use sequential scan or experimental R-tree

## Strengths

- OLAP engine — good for analytical workloads on columnar data
- Clean extension architecture
- Embeddable (in-process)
- Active development community

## Limitations

- **No integration between spatial and VSS extensions**
- **HNSW index incompatible with filtered queries** — any WHERE clause disables ANN index usage
- Extensions developed independently with no coordination
- Experimental spatial R-tree index

## Relevance to StarRocks

- **Architecture parallel**: DuckDB's extension model is similar to StarRocks' approach of having separate spatial functions and vector indexes
- **Same limitation**: Without index-level integration, combined queries fall back to sequential scan
- **Cautionary example**: Building two separate extensions and hoping the query planner combines them is insufficient
- **Lesson**: The hybrid index must be designed as a single integrated structure from the start
