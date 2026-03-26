# Phase 05 Benchmark Spec

## In-Scope Comparisons

- `A3` vs `A4`
- optional `A4` vs `A5`

## Benchmark Questions

1. Does planner adaptation improve mixed-workload performance?
2. Does planner fallback avoid pathological cases where partitioned search is not ideal?
3. If enabled experimentally, does correlation-aware tuning beat plain selectivity-based planning?

## Required Workload Mix

- tiny selective spatial queries
- moderate selective queries
- broad region queries
- boundary-heavy queries
- positive, zero, and negative correlation datasets for optional `A5`

## Metrics

- Recall@K
- latency percentiles
- QPS
- strategy selection distribution
- forced-strategy vs planner-selected delta

## Expected Readout

- `A4` should outperform `A3` on mixed workloads, not necessarily on every single query class
- `A5` should remain optional until it shows a reproducible win without destabilizing defaults

## Result Recording

Store future outputs under:

- `results/ablations/phase_05_a4_*`
- `results/ablations/phase_05_a5_*`
