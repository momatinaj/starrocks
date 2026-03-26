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
| **Correlation sensitivity** | How does **spatial–vector correlation** (positive / zero / negative ρ) affect strategy choice, recall, and oversampling? |
| **Build cost** | How expensive is index construction? |
| **Storage cost** | How much additional disk space is needed? |
| **Memory cost** | How much additional RAM is needed? |

## Evaluation Phases

### Phase 1: Micro-benchmarks (In-Process)
- Single-segment, single-thread tests
- Focus: Recall@K, per-query latency, memory usage
- Tool: Google Benchmark framework + custom harness
- Location: `be/test/storage/index/hybrid_vector_index_test.cpp`

### Phase 2: Integration Tests (Single Node)
- Full StarRocks instance with SQL queries
- Focus: End-to-end latency, QPS, query planner behavior
- Tool: SQL test framework (`test/sql/`)
- Location: `test/sql/test_hybrid_vector_index/`

### Phase 3: Scale Tests (Multi-Node)
- Distributed StarRocks cluster
- Focus: Scalability, cross-tablet merging, shared-data mode
- Tool: Cluster test harness
- Location: `test/sql/test_hybrid_vector_index_scale/`

## Navigation

- [Datasets](datasets.md) — Data sources and **ρ-controlled** synthetic generation
- [Metrics](metrics.md) — What and how to measure
- [Test Queries](test_queries.md) — Representative query patterns
- [Baseline Results](baseline_results.md) — Expected naive approach performance
