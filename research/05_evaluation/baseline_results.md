# Baseline Results

## Status

This document will contain results from baseline approaches once benchmarking is implemented.

## Expected Results (Analytical Estimates)

Based on complexity analysis from `03_algorithmic_design/benchmarks/expected_baselines.md`, with parameters:
- n = 100K vectors per segment
- D = 512 dimensions
- k = 10
- M = 16 (HNSW connections)
- efSearch = 200

### Sequential Scan (No Index)

| Selectivity | Latency Estimate | Recall |
|-------------|-----------------|--------|
| Any | 50-100 ms | 1.00 |

### Spatial Filter → Brute-Force ANN

| Selectivity | Spatial Scan | Vector Distances | Total | Recall |
|-------------|-------------|-----------------|-------|--------|
| 0.1% | ~5 ms | ~0.5 ms (100 rows) | ~5.5 ms | 1.00 |
| 1% | ~5 ms | ~5 ms (1K rows) | ~10 ms | 1.00 |
| 5% | ~5 ms | ~25 ms (5K rows) | ~30 ms | 1.00 |
| 10% | ~5 ms | ~50 ms (10K rows) | ~55 ms | 1.00 |
| 50% | ~5 ms | ~250 ms (50K rows) | ~255 ms | 1.00 |

### HNSW → Post-Filter (Oversampled)

| Selectivity | Oversample K' | HNSW Search | Filter | Total | Recall |
|-------------|--------------|-------------|--------|-------|--------|
| 0.1% | 10,000 | ~50 ms | ~1 ms | ~51 ms | ~0.70 |
| 1% | 1,000 | ~5 ms | ~0.5 ms | ~5.5 ms | ~0.85 |
| 10% | 100 | ~2 ms | ~0.1 ms | ~2.1 ms | ~0.90 |
| 50% | 20 | ~1.5 ms | ~0.05 ms | ~1.5 ms | ~0.95 |
| 90% | 11 | ~1.2 ms | ~0.02 ms | ~1.2 ms | ~0.98 |

### Cascade (Spatial Filter → HNSW with Delete Filter)

| Selectivity | Spatial Scan | HNSW Search | Total | Recall |
|-------------|-------------|-------------|-------|--------|
| 0.1% | ~5 ms | ~30 ms (degraded) | ~35 ms | ~0.80 |
| 1% | ~5 ms | ~15 ms (degraded) | ~20 ms | ~0.85 |
| 5% | ~5 ms | ~8 ms | ~13 ms | ~0.90 |
| 10% | ~5 ms | ~5 ms | ~10 ms | ~0.93 |
| 50% | ~5 ms | ~2 ms | ~7 ms | ~0.95 |
| 90% | ~5 ms | ~1.5 ms | ~6.5 ms | ~0.97 |

## Hybrid Index Targets

| Selectivity | Cascade Baseline | Hybrid Target | Target Speedup |
|-------------|-----------------|---------------|----------------|
| 0.1% | ~35 ms / 0.80 recall | ~5 ms / 0.95 recall | 7x faster, better recall |
| 1% | ~20 ms / 0.85 recall | ~5 ms / 0.95 recall | 4x faster, better recall |
| 5% | ~13 ms / 0.90 recall | ~4 ms / 0.95 recall | 3x faster, better recall |
| 10% | ~10 ms / 0.93 recall | ~4 ms / 0.95 recall | 2.5x faster, better recall |
| 50% | ~7 ms / 0.95 recall | ~3 ms / 0.95 recall | 2x faster, same recall |
| 90% | ~6.5 ms / 0.97 recall | ~3 ms / 0.97 recall | 2x faster, same recall |

## Recording Template

When actual benchmarks are run, record results in this format:

```markdown
### [Approach Name] on [Dataset]

**Date**: YYYY-MM-DD
**Configuration**: n=100K, D=512, M=16, efSearch=200, S2 level=12, k=10

| σ_s | p50 (ms) | p95 (ms) | p99 (ms) | QPS | Recall@10 |
|-----|---------|---------|---------|-----|-----------|
| 0.001 | | | | | |
| 0.01 | | | | | |
| 0.05 | | | | | |
| 0.10 | | | | | |
| 0.25 | | | | | |
| 0.50 | | | | | |
| 0.90 | | | | | |

**Notes**: [Any observations about performance characteristics]
```

## Next Steps

1. Implement synthetic dataset generator
2. Implement brute-force ground truth computation
3. Run baseline benchmarks on synthetic data
4. Record actual results in this document
5. Use results to validate analytical estimates above
6. Iterate on hybrid index design based on actual baseline performance
