# Ablation Matrix

## Purpose

This document is the benchmark contract for the execution program. It defines:

- what counts as a baseline
- what optimization is introduced in each ablation
- which metrics must be collected
- which benchmark axes are mandatory

## Baselines

### `B0`: Spatial Filter + Exact Distance

- Apply spatial filter first
- Compute exact vector distance on survivors
- Use as the correctness and tiny-selectivity baseline

### `B1`: Existing Vector-First ANN + Spatial Post-Filter

- Use current StarRocks vector path with oversampling/post-filter semantics
- Represents the current non-spatially-aware ANN baseline

### `B2`: Planner Fallback Only

- No new spatial-partitioned index
- FE/BE path recognizes spatial+vector pattern and chooses fallback behavior
- This becomes the immediate production-safe baseline for the program

## Ablations

### `A0`: Existing Vector Index Only

- Current vector index behavior
- No spatial-aware planner path

### `A1`: Spatial Planner Fallback

- Query recognition + fallback selection
- No new hybrid index

### `A2`: S2 Partitioning Only

- S2 partitioned HNSW per segment
- No boundary repair
- No advanced planner strategy selection beyond minimum routing

### `A3`: `A2` + Boundary Repair

- Add one isolated repair layer, recommended first: cross-partition stitching

### `A4`: `A3` + Planner Selection

- Planner chooses between brute-force, fallback, partitioned, and repaired modes using spatial selectivity

### `A5`: `A4` + Optional Correlation-Aware Tuning

- Experimental only
- Allows planner/runtime decisions to read a future `ρ̂` or user hint

## Feature Toggle Contract

| Toggle | Meaning | Used in |
|--------|---------|---------|
| `use_spatial_vector_fallback` | Enable fallback path without new hybrid index | `A1`, `B2` |
| `use_s2_partitioned_hnsw` | Enable partitioned index build/read path | `A2+` |
| `use_boundary_repair` | Enable repair layer on top of partitioned index | `A3+` |
| `use_selectivity_planner_choice` | Enable planner-driven strategy selection | `A4+` |
| `use_correlation_hint` | Enable optional correlation-aware adjustments | `A5` |

These names are logical placeholders for the execution program; final code-level names can differ.

## Mandatory Metrics

- Recall@K
- p50 latency
- p95 latency
- p99 latency
- QPS
- build time
- index size
- memory overhead
- compaction overhead
- boundary-region recall degradation

## Mandatory Benchmark Axes

### Query axes

- geometry type: bbox, polygon, radius
- query locality: interior-heavy vs boundary-heavy
- `k`: 1, 10, 50, 100

### Data axes

- spatial selectivity buckets
- S2 level / spatial granularity
- segment density profile
- positive / zero / negative spatial-vector correlation

### System axes

- fallback vs partitioned vs repaired
- HNSW `M`
- `efConstruction`
- `efSearch`
- minimum partition build threshold

## Benchmark Matrix

| Case ID | Baseline / Ablation | Partitioned | Repair | Planner choice | Correlation-aware | Must run |
|---------|---------------------|-------------|--------|----------------|-------------------|----------|
| `C0` | `B0` | No | No | No | No | Yes |
| `C1` | `B1` | No | No | No | No | Yes |
| `C2` | `B2` / `A1` | No | No | Minimal fallback | No | Yes |
| `C3` | `A2` | Yes | No | Minimal routing | No | Yes |
| `C4` | `A3` | Yes | Yes | Minimal routing | No | Yes |
| `C5` | `A4` | Yes | Yes | Yes | No | Yes |
| `C6` | `A5` | Yes | Yes | Yes | Yes | Optional |

## Required Comparisons

### Core comparisons

- `B0` vs `B1`
- `B2` vs `B1`
- `A2` vs `B2`
- `A3` vs `A2`
- `A4` vs `A3`

### Correlation comparisons

For each of `A2`, `A3`, `A4`, run:

- positive correlation workload
- zero correlation workload
- negative correlation workload

### Boundary sensitivity comparisons

For `A2` and `A3`, use the same:

- S2 level
- dataset
- query shape
- selectivity

Only boundary intensity should change.

## Success Thresholds

These are planning thresholds, not final product commitments:

- `A2` should show measurable pruning benefit over `B2` on selective spatial queries
- `A3` should improve boundary-heavy recall over `A2`
- `A4` should avoid pathological slowdowns by choosing fallback when partitioning is not beneficial
- `A5` must not become default behavior without a measurable win over `A4`

## Benchmark Output Locations

- `results/baseline/`
- `results/ablations/`
- `results/scale/`
- `results/regression/`

Each future result drop should record:

- git revision
- dataset
- benchmark configuration
- feature toggles enabled
- summary table
- notes on anomalies
