# Evaluation Framework

## Purpose

This section defines how to benchmark and validate the hybrid spatial+vector index. The evaluation framework provides:

1. **Datasets** — real and synthetic data for testing
2. **Metrics** — what we measure and how
3. **Test Queries** — representative query patterns
4. **Baselines** — naive approaches to compare against

## Evaluation Goals

| Goal | Question Answered |
|------|-------------------|
| **Correctness** | Does the hybrid index return the right results? (Recall@K) |
| **Performance** | How fast are queries? (Latency, QPS) |
| **Scalability** | How does performance change with data size? |
| **Selectivity sensitivity** | How does spatial selectivity affect performance? |
| **Correlation sensitivity** | How does spatial-vector correlation affect strategy choice, recall, and oversampling? |
| **Build cost** | How expensive is index construction? |
| **Storage cost** | How much additional disk space is needed? |
| **Memory cost** | How much additional RAM is needed? |

## Current Benchmark Implementation

The benchmark suite lives in `research/06_s2_hnsw_execution_program/benchmarks/`:

- `run_benchmark.py` — generates synthetic data, loads into StarRocks, executes queries, computes recall
- `run_full_benchmark.sh` — one-command B0 vs ACORN-1 vs Grid-HNSW comparison
- `generate_report.py` — HTML report with latency/recall charts
- `compare_results.py` — side-by-side text comparison table

Run the benchmark:

```bash
cd research/06_s2_hnsw_execution_program/benchmarks/
./run_full_benchmark.sh
```

See [06_s2_hnsw_execution_program/PRODUCTION_GUIDE.md](../06_s2_hnsw_execution_program/PRODUCTION_GUIDE.md) for the full benchmarking guide.

## Navigation

- [Datasets](datasets.md) — Data sources and rho-controlled synthetic generation
- [Metrics](metrics.md) — What and how to measure
- [Test Queries](test_queries.md) — Representative query patterns
- [Baseline Results](baseline_results.md) — Expected naive approach performance
