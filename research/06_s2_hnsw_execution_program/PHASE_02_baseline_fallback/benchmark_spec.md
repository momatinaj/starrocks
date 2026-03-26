# Phase 02 Benchmark Spec

## In-Scope Comparisons

- `B0` vs `B2`
- `B1` vs `B2`

## Benchmark Questions

1. Is planner/runtime fallback materially better than naive vector-first post-filtering?
2. At what selectivity does exact-on-filtered-candidates beat ANN reuse?
3. Does the fallback path provide a stable baseline for later hybrid-index phases?

## Required Query Sets

- highly selective radius queries
- moderately selective polygon queries
- broad city/region queries
- both interior-heavy and boundary-heavy variants where possible

## Metrics

- Recall@K
- latency percentiles
- QPS
- candidate count after spatial filtering
- oversampling cost compared to vector-first baseline

## Expected Readout

- `B2` should beat `B1` on selective spatial workloads
- `B2` may be comparable to `B0` on tiny candidate sets
- `B2` is not expected to match the future partitioned index on selective workloads

## Result Recording

Store future outputs under:

- `results/baseline/phase_02_*`
