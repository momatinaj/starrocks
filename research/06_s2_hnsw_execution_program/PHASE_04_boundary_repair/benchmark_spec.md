# Phase 04 Benchmark Spec

## In-Scope Comparisons

- `A2` vs `A3`

## Benchmark Questions

1. How much recall does boundary repair recover?
2. What is the latency overhead of repair?
3. What is the build-time and storage overhead of repair metadata?

## Required Query Sets

- boundary-heavy polygon queries
- radius queries centered near partition boundaries
- interior-heavy control queries

## Required Metrics

- Recall@K
- boundary-specific recall loss
- latency percentiles
- QPS
- build time delta from `A2`
- index size delta from `A2`

## Expected Readout

- `A3` should improve recall on boundary-heavy cases
- `A3` should not materially regress interior-heavy cases
- overhead should be visible and attributable to repair only

## Result Recording

Store future outputs under:

- `results/ablations/phase_04_a3_*`
