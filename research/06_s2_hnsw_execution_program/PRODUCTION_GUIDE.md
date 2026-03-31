# Spatial-Vector Search in StarRocks — Production Guide

Complete guide for deploying and using spatial-vector hybrid search with ACORN-1, ACORN-gamma, and Grid-HNSW indexes in a custom StarRocks build.

---

## Table of Contents

1. [Overview](#overview)
2. [Architecture](#architecture)
3. [Deployment](#deployment)
4. [Creating Tables with Spatial-Vector Indexes](#creating-tables)
5. [Loading Your Own Data](#loading-data)
6. [Writing Queries](#writing-queries)
7. [Tuning Parameters](#tuning-parameters)
8. [Benchmarking with Your Data](#benchmarking)
9. [Production Checklist](#production-checklist)
10. [Troubleshooting](#troubleshooting)

---

## 1. Overview 

This custom StarRocks build adds three new vector index types that combine **spatial filtering** with **approximate nearest neighbor (ANN)** search:


| Index Type        | How It Works                                                                                                                                                     | Best For                                                                                                                         |
| ----------------- | ---------------------------------------------------------------------------------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| **ACORN-1**       | Predicate-aware HNSW graph traversal. During search, only spatially-qualifying nodes are visited, using 2-hop expansion to bridge over non-qualifying nodes.     | Queries with moderate-to-broad spatial predicates (5km+ radius, metro polygons). No index rebuild needed when predicates change. |
| **ACORN-gamma**   | Same search as ACORN-1 but built on a denser graph (gamma*M neighbors per node). More neighbors mean the 2-hop expansion covers more qualifying candidates.     | Same use case as ACORN-1 but with higher recall at the cost of larger index size and longer build time.                          |
| **Grid-HNSW**     | Spatially partitions data using S2 cells, builds per-partition HNSW indexes, searches only relevant partitions.                                                  | Queries with consistent spatial patterns. Faster than ACORN but requires lat/lng columns at index creation.                      |

---

## 2. Architecture 

```
SQL Query (e.g. "find 10 nearest vectors within 5km of a point")
    │
    ▼
┌─────────────────────────────────────────────────┐
│  Frontend (FE) — Java                            │
│  RewriteToVectorPlanRule detects:                 │
│    - approx_l2_distance() call  → vector search  │
│    - st_contains / st_distance  → spatial pred   │
│  Sets useAcorn=true or useGridHnsw=true          │
│  Serializes params via Thrift → BE               │
└─────────────────────────────────────────────────┘
    │
    ▼
┌─────────────────────────────────────────────────┐
│  Backend (BE) — C++                              │
│  SegmentIterator detects index type from meta:   │
│    - "acorn"       → AcornIndexReader (gamma=1)  │
│    - "acorn_gamma" → AcornIndexReader (gamma>1)  │
│    - "grid_hnsw"   → SpatialVectorIndexReader    │
│  Loads .vi file, parses HNSW graph, runs search  │
│  Returns top-K row IDs + distances               │
└─────────────────────────────────────────────────┘
```

---

## 3. Deployment 

### Prerequisites

- Docker and Docker Compose
- MySQL client (for connecting to StarRocks)
- Python 3.8+ with `pymysql`, `numpy` (for benchmarking)
- The custom StarRocks images (built from the `feature/spatial-vector-fallback` branch)

### Starting the Cluster

```bash
# From the repo root:
docker-compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be
```

Wait ~40 seconds for FE and BE to initialize, then verify:

```bash
# Check FE is ready
mysql -h 127.0.0.1 -P 9030 -u root -e "SHOW FRONTENDS\G"

# Check BE is registered and alive
mysql -h 127.0.0.1 -P 9030 -u root -e "SHOW BACKENDS\G"
```

You should see `Alive: true` in the BACKENDS output.

### Building from Source (for developers)

```bash
# Build FE
docker-compose -f docker-compose.dev.yml run --rm build-fe

# Build BE
docker-compose -f docker-compose.dev.yml run --rm build-be

# Restart services
docker-compose -f docker-compose.dev.yml restart starrocks-custom-fe starrocks-custom-be
```

---

## 4. Creating Tables with Spatial-Vector Indexes 

### ACORN-1 Index

ACORN indexes are defined on the vector column only. Spatial filtering happens at query time using any columns.

```sql
CREATE DATABASE IF NOT EXISTS my_database;
USE my_database;

CREATE TABLE my_vectors (
    id          BIGINT       NOT NULL,
    lat         DOUBLE       NOT NULL,
    lng         DOUBLE       NOT NULL,
    vector_col  ARRAY<FLOAT> NOT NULL,
    category    VARCHAR(64),
    created_at  DATETIME,
    INDEX vec_idx (vector_col) USING VECTOR(
        "index_type"       = "acorn",
        "dim"              = "128",        -- must match your vector dimension
        "metric_type"      = "l2_distance",
        "is_vector_normed" = "false",
        "M"                = "16",         -- HNSW connectivity (16-64)
        "efconstruction"   = "40"          -- build quality (40-200)
    )
) ENGINE=OLAP
DUPLICATE KEY(id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES("replication_num" = "1");
```

### ACORN-gamma Index

ACORN-gamma builds a denser graph at construction time. The `gamma` parameter (default 2) multiplies the number of neighbors per node, giving the 2-hop search more qualifying candidates to find. The `M` you specify is the *base* M; the actual graph is built with `M * gamma` neighbors.

```sql
CREATE TABLE my_vectors_gamma (
    id          BIGINT       NOT NULL,
    lat         DOUBLE       NOT NULL,
    lng         DOUBLE       NOT NULL,
    vector_col  ARRAY<FLOAT> NOT NULL,
    INDEX vec_idx (vector_col) USING VECTOR(
        "index_type"       = "acorn_gamma",
        "dim"              = "128",
        "metric_type"      = "l2_distance",
        "is_vector_normed" = "false",
        "M"                = "16",         -- base M (actual graph: M * gamma = 32)
        "efconstruction"   = "40",
        "gamma"            = "2"           -- graph density multiplier (>= 2)
    )
) ENGINE=OLAP
DUPLICATE KEY(id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES("replication_num" = "1");
```

**Trade-offs vs ACORN-1:**
- Higher recall under selective predicates (more graph connectivity)
- Larger index on disk (~gamma times larger neighbor lists)
- Longer build time (more neighbors to compute during construction)
- Queries against the index syntax are identical to ACORN-1

### Grid-HNSW Index

Grid-HNSW indexes require specifying lat/lng columns at creation time.

```sql
CREATE TABLE my_vectors_grid (
    id          BIGINT       NOT NULL,
    lat         DOUBLE       NOT NULL,
    lng         DOUBLE       NOT NULL,
    vector_col  ARRAY<FLOAT> NOT NULL,
    INDEX vec_idx (vector_col) USING VECTOR(
        "index_type"       = "grid_hnsw",
        "dim"              = "128",
        "metric_type"      = "l2_distance",
        "is_vector_normed" = "false",
        "M"                = "16",
        "efconstruction"   = "40",
        "s2_level"         = "12",         -- S2 cell level (10-14)
        "lat_column"       = "lat",
        "lng_column"       = "lng"
    )
) ENGINE=OLAP
DUPLICATE KEY(id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES("replication_num" = "1");
```

### Standard HNSW (no spatial awareness — baseline comparison)

```sql
CREATE TABLE my_vectors_plain (
    id          BIGINT       NOT NULL,
    lat         DOUBLE       NOT NULL,
    lng         DOUBLE       NOT NULL,
    vector_col  ARRAY<FLOAT> NOT NULL,
    INDEX vec_idx (vector_col) USING VECTOR(
        "index_type"       = "hnsw",
        "dim"              = "128",
        "metric_type"      = "l2_distance",
        "is_vector_normed" = "false",
        "M"                = "16",
        "efconstruction"   = "40"
    )
) ENGINE=OLAP
DUPLICATE KEY(id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES("replication_num" = "1");
```

---

## 5. Loading Your Own Data 

### From CSV / Files

```sql
-- Stream Load (recommended for large files)
curl --location-trusted -u root: \
  -H "column_separator:," \
  -H "columns: id, lat, lng, vector_col" \
  -T my_data.csv \
  http://127.0.0.1:8040/api/my_database/my_vectors/_stream_load
```

### From Python

```python
import pymysql
import numpy as np

conn = pymysql.connect(host='127.0.0.1', port=9030, user='root', database='my_database')
cursor = conn.cursor()

# Insert row by row (small datasets)
for i in range(1000):
    vec = np.random.randn(128).tolist()
    lat, lng = 37.7749 + np.random.randn() * 0.05, -122.4194 + np.random.randn() * 0.05
    vec_str = "[" + ",".join(f"{v:.6f}" for v in vec) + "]"
    cursor.execute(
        f"INSERT INTO my_vectors VALUES ({i}, {lat}, {lng}, {vec_str}, 'cat_a', NOW())"
    )

conn.commit()
conn.close()
```

### From Another StarRocks Table

```sql
INSERT INTO my_vectors_acorn
SELECT id, lat, lng, vector_col, category, created_at
FROM my_vectors_plain;
```

### Important: After Loading Data

After inserting data, the vector index is built automatically during compaction. To force immediate index build:

```sql
ALTER TABLE my_vectors COMPACT;
```

Wait a few seconds, then verify the index exists:

```sql
SHOW INDEX FROM my_vectors;
```

---

## 6. Writing Queries 

### Basic Vector Search (no spatial filter)

```sql
SELECT id, lat, lng,
       approx_l2_distance(vector_col, [0.1, 0.2, ...]) AS dist
FROM my_vectors
ORDER BY dist
LIMIT 10;
```

### Vector + Radius Filter (ACORN or Grid-HNSW)

```sql
-- Find 10 nearest vectors within 5km of San Francisco city center
SELECT id, lat, lng,
       approx_l2_distance(vector_col, [0.1, 0.2, ...]) AS dist
FROM my_vectors
WHERE st_distance_sphere(st_point(lng, lat), st_point(-122.4194, 37.7749)) <= 5000
ORDER BY dist
LIMIT 10;
```

### Vector + Polygon Filter

```sql
-- Find 10 nearest vectors inside a bounding box
SELECT id, lat, lng,
       approx_l2_distance(vector_col, [0.1, 0.2, ...]) AS dist
FROM my_vectors
WHERE st_contains(
    ST_GeomFromText('POLYGON((-122.45 37.75, -122.35 37.75,
                               -122.35 37.80, -122.45 37.80,
                               -122.45 37.75))'),
    st_point(lng, lat)
)
ORDER BY dist
LIMIT 10;
```

### Vector Search on Standard HNSW (for comparison)

The same SQL works on tables with standard HNSW indexes — the spatial predicate is applied as a post-filter instead of being integrated into the search.

---

## 7. Tuning Parameters 

### Index Build Parameters


| Parameter                       | Default | Range  | Effect                                                                   |
| ------------------------------- | ------- | ------ | ------------------------------------------------------------------------ |
| `M`                             | 16      | 8-64   | Graph connectivity. Higher = better recall but more memory/build time    |
| `efconstruction`                | 40      | 40-500 | Build-time search quality. Higher = better graph but slower build        |
| `gamma` (ACORN_GAMMA only)      | 2       | 2-8    | Graph density multiplier. Actual neighbors = M * gamma. Higher = better recall, larger index |
| `s2_level` (Grid-HNSW only)     | 12      | 10-14  | Spatial partition granularity. 12 = ~3km cells, 14 = ~300m cells         |
| `dim`                           | —       | 1-2048 | Must match your vector dimension exactly                                 |


### Query-Time Parameters

The query planner automatically sets `ef_search` based on K and whether a predicate is present:

- Without predicate: `ef_search = max(40, K * 4)`
- With predicate (ACORN): `ef_search = max(400, K * 40)`

For Grid-HNSW, the number of S2 cells searched is determined automatically from the spatial predicate.

### Which Index to Choose?


| Scenario                                            | Recommendation                                                        |
| --------------------------------------------------- | --------------------------------------------------------------------- |
| Mixed spatial predicates (various radii, polygons)  | **ACORN-1** — adapts at query time, smallest index                    |
| Recall is critical under selective predicates        | **ACORN-gamma** — denser graph compensates for filtered-out neighbors |
| Fixed spatial pattern (always same region)           | **Grid-HNSW** — fastest, but spatial partitioning is baked in         |
| Very selective predicates (<1km radius)              | **ACORN-gamma** (gamma=4) or **ACORN-1** with higher ef_search       |
| No spatial predicates needed                         | **Standard HNSW**                                                     |
| Highest possible recall required                   | **Brute force** (no index)                                    |


---

## 8. Benchmarking with Your Data 

### Quick Re-run (skip baseline + skip data loading)

```bash
cd research/06_s2_hnsw_execution_program/benchmarks

# Fastest: only re-run ACORN queries using existing data and cached B0 results
./run_full_benchmark.sh --skip-load --skip-baseline --only acorn

# Run only ACORN-gamma
./run_full_benchmark.sh --skip-load --skip-baseline --only acorn_gamma

# Re-run ACORN, ACORN-gamma, and Grid-HNSW, skip baseline
./run_full_benchmark.sh --skip-load --skip-baseline

# Full benchmark including baseline
./run_full_benchmark.sh --skip-load
```

### When Do You Need to Reload Data?


| Situation                        | `--skip-load`?    | `--skip-baseline`?        |
| -------------------------------- | ----------------- | ------------------------- |
| First run ever                   | No                | No                        |
| Changed `--rows` or `--dim`      | No                | No                        |
| Code changes only (same data)    | Yes               | Yes (if B0 results exist) |
| Changed index parameters (M, ef) | No (need rebuild) | Yes                       |
| Same server, different session   | Yes               | Yes                       |
| Different server                 | No                | No                        |


### Custom Benchmark with Your Data

To benchmark with your own data instead of synthetic data:

1. **Create tables manually** using the DDL from Section 4
2. **Load your data** using the methods from Section 5
3. **Run queries** manually and measure latency:

```python
import pymysql
import time

conn = pymysql.connect(host='127.0.0.1', port=9030, user='root', database='my_database')
cursor = conn.cursor()

query_vector = [0.1, 0.2, ...]  # your query vector
vec_str = "[" + ",".join(str(v) for v in query_vector) + "]"

start = time.time()
cursor.execute(f"""
    SELECT id, approx_l2_distance(vector_col, {vec_str}) AS dist
    FROM my_vectors
    WHERE st_distance_sphere(st_point(lng, lat), st_point(-122.4194, 37.7749)) <= 5000
    ORDER BY dist LIMIT 10
""")
results = cursor.fetchall()
elapsed_ms = (time.time() - start) * 1000

print(f"Latency: {elapsed_ms:.1f}ms, Results: {len(results)}")
for row in results:
    print(f"  id={row[0]}, dist={row[1]:.4f}")
```

1. **Compare recall** against brute force by running the same query on a table without a vector index.

### Benchmark Parameters

```bash
./run_full_benchmark.sh \
  --host 127.0.0.1 \
  --port 9030 \
  --rows 100000 \      # number of synthetic rows
  --dim 128 \           # vector dimension
  --k 10 \              # top-K results
  --queries 50 \        # queries per spec
  --warmup 5            # warmup queries (not measured)
```

---

## 9. Production Checklist 

Before using in production, verify:

### Correctness

- **Recall is acceptable** for your use case (run benchmarks with YOUR data)
- **Results match brute force** for representative queries (spot-check 10-20 queries)
- **Edge cases work**: empty results, no spatial predicate, all-pass predicate
- **Data integrity**: insert + compaction + query cycle works end-to-end

### Performance

- **Latency meets SLA** under expected query load
- **Index build time** is acceptable for your data size and update frequency
- **Memory usage** is within bounds (check BE logs for OOM warnings)
- **Concurrent queries** don't cause excessive latency spikes

### Operational

- **Monitoring**: BE logs contain search diagnostics (grep for "ACORN" or "GRID")
- **Recovery**: cluster restart preserves indexes (no rebuild needed)
- **Compaction**: vector indexes survive compaction cycles
- **Replication**: replicated tablets maintain index consistency

### Limitations (Known)

- ACORN and Grid-HNSW indexes are only supported on `DUPLICATE KEY` tables
- Vector dimension must be specified at table creation time and cannot change
- Grid-HNSW requires lat/lng columns to be present and populated for all rows
- The `metric_type` must be `l2_distance` (cosine/IP not yet supported for spatial modes)
- Very selective predicates (<1km radius on sparse data) may have lower recall

---

## 10. Troubleshooting 

### Common Issues

**Q: Query returns no results or very few results**
Check BE logs for ACORN/GRID diagnostics:

```bash
docker logs starrocks-custom-be 2>&1 | grep "ACORN\|GRID\|HNSW"
```

Look for:

- `M=0` → graph connectivity bug (should be auto-recovered; report if seen)
- `ACORN reader init failed` → index file parsing error
- `level0_neighbors=0` → graph has no edges

**Q: ACORN search is slower than expected**
The predicate might be too selective, causing excessive graph exploration. Check:

```bash
docker logs starrocks-custom-be 2>&1 | grep "ACORN predicate stats"
```

If `rate=` is below 1%, the predicate is very selective. Consider using a broader radius.

**Q: Index doesn't seem to be used**
Verify the index exists and is built:

```sql
SHOW INDEX FROM my_table;
SHOW TABLET FROM my_table;
```

Force compaction if needed: `ALTER TABLE my_table COMPACT;`

**Q: How do I check which index type is being used?**
Check BE logs during query execution:

```bash
docker logs starrocks-custom-be 2>&1 | grep "ACORN search\|spatial_vector_index"
```

**Q: Cluster won't start or BE keeps restarting**

```bash
docker logs starrocks-custom-fe 2>&1 | tail -50
docker logs starrocks-custom-be 2>&1 | tail -50
```

Common causes: FE needs 30-40s to initialize before BE can register.

---

## Quick Reference Card

```
# Start cluster
docker compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be

# Verify health
mysql -h 127.0.0.1 -P 9030 -u root -e "SHOW BACKENDS\G"

# Run quick benchmark (ACORN only, skip baseline)
cd research/06_s2_hnsw_execution_program/benchmarks
./run_full_benchmark.sh --skip-load --skip-baseline --only acorn

# Run full comparison
./run_full_benchmark.sh --skip-load

# Check BE diagnostics
docker logs starrocks-custom-be 2>&1 | grep "ACORN\|GRID\|HNSW" | tail -30

# Stop cluster
docker compose -f docker-compose.dev.yml down
```

