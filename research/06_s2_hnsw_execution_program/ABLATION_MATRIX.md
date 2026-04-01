# Ablation Matrix

## Purpose

Benchmark contract defining baselines, configurations, and metrics for comparing spatial-vector search approaches.

## Configurations

### Baselines

| ID | Name | Description | Benchmark Mode |
|----|------|-------------|---------------|
| **B0** | Brute Force | Spatial filter + exact vector distance on all survivors. Ground truth for recall. | `--mode b0` |

### Shipped Configurations

| ID | Name | Description | Benchmark Mode | Index Type |
|----|------|-------------|---------------|------------|
| **ACORN** | ACORN-1 Search | Predicate-aware HNSW traversal with 2-hop expansion. Spatial predicate evaluated during graph walk. | `--mode acorn` | `acorn` |
| **GRID** | Grid-HNSW | S2-partitioned HNSW. Per-cell indexes, FE computes cell covering, BE searches matching partitions. | `--mode grid` | `grid_hnsw` |

### Legacy / Manual-Only Configurations

These modes are supported in `run_benchmark.py` but are not part of the default benchmark pipeline:

| ID | Name | Description | Benchmark Mode |
|----|------|-------------|---------------|
| **A0** | Official HNSW | Standard StarRocks HNSW with no spatial awareness. Requires official image on port 19030. | `--mode a0` |
| **B2** | Planner Fallback | FE recognizes spatial+vector pattern, BE post-filters standard HNSW results. | `--mode b2` |

## Feature Toggle Mapping

| Logical Toggle | Actual Code Surface | Where Set |
|----------------|-------------------|-----------|
| Use ACORN search | `VectorSearchOptions.useAcorn = true` | `RewriteToVectorPlanRule.java` |
| Use Grid-HNSW search | `VectorSearchOptions.useGridHnsw = true` | `RewriteToVectorPlanRule.java` |
| Fallback mode | `VectorSearchOptions.fallbackMode = "SPATIAL_FILTER_EXACT"` | `RewriteToVectorPlanRule.java` |
| ACORN predicate type | `acorn_predicate_type` in query params | Thrift `TVectorSearchOptions` |
| ACORN spatial params | `acorn_center_lat`, `acorn_center_lng`, `acorn_radius_m`, `acorn_polygon_wkt` | Thrift `TVectorSearchOptions` |

## Mandatory Metrics

- Recall@K (vs B0 ground truth)
- p50 latency (ms)
- p95 latency (ms)
- Speedup vs B0

## Benchmark Axes

### Query axes

- Geometry type: radius (1km, 5km, 20km), polygon (downtown, metro)
- Selectivity: very selective (1km) to broad (20km)

### Data axes

- Row count: 100K (default), 500K, 1M for scale tests
- Vector dimension: 128 (default)

### System axes

- HNSW `M`: 16 (default)
- `efConstruction`: 40 (default)
- `efSearch`: auto-tuned (40 without predicate, 400 with predicate)
- Grid-HNSW `s2_level`: 12 (default)
- Grid-HNSW `oversample`: 5.0 (default), `max_cover_cells`: 500 (default)

## Default Benchmark Matrix

| Case | Configuration | Command |
|------|--------------|---------|
| Full comparison | B0 vs ACORN vs GRID | `./run_full_benchmark.sh` |
| ACORN only | ACORN (skip baseline) | `./run_full_benchmark.sh --skip-load --skip-baseline --only acorn` |
| Grid only | GRID (skip baseline) | `./run_full_benchmark.sh --skip-load --skip-baseline --only grid` |
| Scale test | 500K rows | `./run_full_benchmark.sh --rows 500000` |

## Benchmark Output

JSON results are written to `benchmarks/results/`. Each result file records:

- Mode, timestamp, row count, dimension
- Per-query-type: p50/p95/p99 latency, recall, result count
- Raw query results for recall computation
