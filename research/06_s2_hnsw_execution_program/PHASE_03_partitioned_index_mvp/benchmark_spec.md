# Phase 03 Benchmark Spec

## In-Scope Comparisons

- `A2` vs `B2`
- `A2` vs `B0` on selective queries

## Benchmark Questions

1. Does S2 partitioning alone deliver latency/QPS benefit over fallback?
2. How much recall is lost near cell boundaries before repair exists?
3. What S2 levels produce reasonable tradeoffs for the MVP?

## Required Query Sets

- interior-heavy selective polygon queries
- boundary-heavy selective polygon queries
- radius queries spanning 1, 2, and many partitions
- broad queries where partition fan-out becomes expensive

## Required Sweeps

- S2 level
- minimum partition threshold
- `efSearch`

## Metrics

- Recall@K
- p50 / p95 / p99 latency
- QPS
- number of matching partitions
- per-query fan-out
- build time and index size

## Expected Readout

- `A2` should beat `B2` on selective interior-heavy queries
- `A2` may underperform on boundary-heavy queries because repair is not yet present
- the MVP should reveal the real cost of partition fan-out directly

## Result Recording

Store future outputs under:

- `results/ablations/phase_03_a2_*`
