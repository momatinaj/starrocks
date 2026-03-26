# SQL Syntax

## CREATE INDEX

### Grammar Extension

In `fe/fe-grammar/src/main/antlr/com/starrocks/grammar/StarRocks.g4`:

```antlr
indexType
    : USING (BITMAP | GIN | NGRAMBF | VECTOR | SPATIAL_VECTOR)
    ;
```

### DDL Syntax

```sql
-- Create hybrid spatial+vector index
CREATE INDEX idx_geo_vector ON table_name (embedding_column)
USING SPATIAL_VECTOR
PROPERTIES (
    -- Spatial properties
    "spatial_column" = "location",
    "s2_level" = "12",
    
    -- Vector properties  
    "index_type" = "HNSW",
    "dim" = "512",
    "metric_type" = "L2",
    "M" = "16",
    "efconstruction" = "200"
);
```

The index is created on the embedding column, with the spatial column specified as a property. This is consistent with the existing VECTOR index syntax (where the index is on the vector column).

### Table Definition Syntax

```sql
CREATE TABLE geo_vectors (
    id BIGINT NOT NULL,
    location VARCHAR NOT NULL COMMENT 'WKB-encoded geometry',
    embedding ARRAY<FLOAT> NOT NULL,
    category VARCHAR,
    created_at DATETIME,
    INDEX idx_geo_vec (embedding) USING SPATIAL_VECTOR
        PROPERTIES (
            "spatial_column" = "location",
            "s2_level" = "12",
            "dim" = "512",
            "metric_type" = "L2",
            "M" = "16",
            "efconstruction" = "200"
        )
) ENGINE=OLAP
DUPLICATE KEY(id)
DISTRIBUTED BY HASH(id) BUCKETS 16;
```

## Query Syntax

### No New Query Syntax Required

The hybrid index is transparent to the query syntax. Users write standard SQL combining spatial functions with vector distance functions:

```sql
-- This query automatically uses the hybrid index if available
SELECT id, category,
       approx_l2_distance(embedding, [0.1, 0.2, ...]) AS dist
FROM geo_vectors
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((-122.42 37.78, -122.40 37.78,
                              -122.40 37.76, -122.42 37.76,
                              -122.42 37.78))'),
    location
)
ORDER BY dist
LIMIT 10;
```

The optimizer recognizes the pattern (spatial predicate + vector distance + ORDER BY + LIMIT) and rewrites it to use the hybrid index.

### Supported Spatial Predicates

| Function | Supported | Notes |
|----------|-----------|-------|
| `ST_Contains(geom, column)` | Yes | Point-in-polygon |
| `ST_Within(column, geom)` | Yes | Reversed ST_Contains |
| `ST_DWithin(column, point, dist)` | Yes | Point-in-radius |
| `ST_Intersects(geom, column)` | Future | Requires geometry-geometry intersection |
| `ST_Distance_Sphere(col_lon, col_lat, lon, lat) < D` | Yes | Distance-based filter |

### Session Variables

```sql
-- Control hybrid index behavior
SET hybrid_vector_index_efsearch = 200;  -- Search beam width
SET hybrid_vector_index_strategy = 'auto';  -- auto, brute_force, filtered_hnsw, partitioned
```

## ALTER TABLE

### Add Hybrid Index to Existing Table

```sql
ALTER TABLE geo_vectors ADD INDEX idx_geo_vec (embedding)
USING SPATIAL_VECTOR
PROPERTIES (
    "spatial_column" = "location",
    "s2_level" = "12",
    "dim" = "512",
    "metric_type" = "L2"
);
```

### Drop Hybrid Index

```sql
ALTER TABLE geo_vectors DROP INDEX idx_geo_vec;
```

### Modify Index Properties

```sql
ALTER TABLE geo_vectors ALTER INDEX idx_geo_vec
SET PROPERTIES ("s2_level" = "14", "efsearch" = "300");
```

## SHOW INDEX

```sql
SHOW INDEX FROM geo_vectors;

+----------+-----------+--------------+------------------+
| IndexName| IndexType | Columns      | Properties       |
+----------+-----------+--------------+------------------+
| idx_vec  | SPATIAL_V | embedding    | s2_level=12,     |
|          | ECTOR     |              | spatial_col=     |
|          |           |              | location, dim=512|
+----------+-----------+--------------+------------------+
```

## EXPLAIN

```sql
EXPLAIN SELECT ...;

-- Output includes:
-- OlapScanNode
--   HybridVectorSearch:
--     strategy: SPATIAL_PARTITIONED
--     spatial_selectivity: 0.03
--     spatial_predicate: ST_Contains(...)
--     vector_distance: approx_l2_distance
--     k: 10
--     matching_partitions: 4
```
