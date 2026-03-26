# Phase 06 Benchmark Spec

## In-Scope Comparisons

- all baselines: `B0`, `B1`, `B2`
- all core ablations: `A2`, `A3`, `A4`
- optional `A5`

## Benchmark Questions

1. Does the full package outperform the baselines on the intended workload family?
2. Are the gains attributable to the expected optimizations?
3. Does any phase introduce unacceptable build, memory, or lifecycle cost?

## Required Suites

### Baseline suite

- brute-force
- current vector behavior
- fallback baseline

### Ablation suite

- partitioning only
- partitioning + repair
- partitioning + repair + planner selection
- optional correlation-aware tuning

### Scale suite

- larger segment sizes
- more partitions
- dense and sparse spatial distributions

### Regression suite

- representative small benchmark set reused over time
- same toggles and fixed configs for change detection

## Required Metrics

- Recall@K
- p50 / p95 / p99 latency
- QPS
- build time
- index size
- memory overhead
- compaction overhead
- boundary recall degradation

## Result Layout

- `results/baseline/`
- `results/ablations/`
- `results/scale/`
- `results/regression/`

Each result drop should include:

- summary table
- environment/configuration
- feature toggles
- dataset and query-set identity
- short interpretation notes

## Success Criteria

- baseline and ablation package is complete
- performance and correctness can be judged together
- future implementation turns can add measured results without restructuring the workspace
