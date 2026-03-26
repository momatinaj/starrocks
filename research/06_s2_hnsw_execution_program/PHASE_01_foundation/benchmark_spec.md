# Phase 01 Benchmark Spec

## Goal

Define the benchmark contract, not to run benchmarks yet.

## Mandatory Baselines

- `B0`: spatial filter + exact distance
- `B1`: existing vector-first ANN + spatial post-filter
- `B2`: planner fallback only

## Mandatory Ablations

- `A2`: S2 partitioning only
- `A3`: `A2` + boundary repair
- `A4`: `A3` + planner selection
- `A5`: optional correlation-aware tuning

## Required Benchmark Axes

- selectivity buckets
- geometry type
- S2 level
- boundary-heavy vs interior-heavy queries
- positive / zero / negative correlation workloads

## Required Metrics

- Recall@K
- p50 / p95 / p99 latency
- QPS
- build time
- index size
- memory overhead
- compaction overhead

## Benchmark Record Format

Every future result drop should include:

- benchmark ID
- baseline or ablation ID
- dataset name
- geometry type
- selectivity bucket
- S2 level
- HNSW parameters
- enabled toggles
- result table
- notes on anomalies

## Success Criteria For Phase 01

- no phase redefines the benchmark contract
- all later benchmark specs can inherit this structure directly
