# Spatial-Vector Execution Program

Implementation of hybrid spatial+vector search indexes in StarRocks.

## Shipped Features

Two new vector index types that integrate spatial filtering directly into the ANN search algorithm:

| Index Type | FE Flag | BE Reader | Status |
|------------|---------|-----------|--------|
| **ACORN-1** | `useAcorn=true` | `AcornIndexReader` | Shipped |
| **Grid-HNSW** | `useGridHnsw=true` | `SpatialVectorIndexReader` | Shipped |
| Planner fallback | `fallbackMode=SPATIAL_FILTER_EXACT` | Standard HNSW + post-filter | Shipped |

## Key Files

### Documentation

| File | Purpose |
|------|---------|
| [PRODUCTION_GUIDE.md](PRODUCTION_GUIDE.md) | Deployment, DDL, queries, tuning, troubleshooting |
| [MASTER_PLAN.md](MASTER_PLAN.md) | Roadmap, phase tracking, deferred work |
| [ABLATION_MATRIX.md](ABLATION_MATRIX.md) | Benchmark configuration definitions |
| [RISKS_AND_ASSUMPTIONS.md](RISKS_AND_ASSUMPTIONS.md) | Risk register with mitigation status |

### Benchmark Suite

| File | Purpose |
|------|---------|
| [benchmarks/run_full_benchmark.sh](benchmarks/run_full_benchmark.sh) | One-command B0 vs ACORN vs Grid-HNSW comparison |
| [benchmarks/run_benchmark.py](benchmarks/run_benchmark.py) | Per-mode benchmark runner (data gen, load, query, recall) |
| [benchmarks/generate_report.py](benchmarks/generate_report.py) | HTML report with latency/recall charts |
| [benchmarks/compare_results.py](benchmarks/compare_results.py) | Side-by-side text comparison table |

### StarRocks Source (key touchpoints)

**Frontend (Java):**
- `fe/fe-core/.../sql/optimizer/rule/transformation/RewriteToVectorPlanRule.java` — routes spatial+vector queries to ACORN / Grid-HNSW / fallback
- `fe/fe-core/.../common/VectorSearchOptions.java` — carries `useAcorn`, `useGridHnsw`, predicate params

**Backend (C++):**
- `be/src/storage/index/vector/acorn_index_reader.{h,cpp}` — ACORN-1 search algorithm
- `be/src/storage/index/vector/hnsw_graph_accessor.{h,cpp}` — Faiss binary parser for HNSW graphs
- `be/src/storage/index/vector/search_predicate_evaluator.{h,cpp}` — spatial predicate evaluation
- `be/src/storage/index/vector/spatial_vector_index_reader.{h,cpp}` — Grid-HNSW partitioned search
- `be/src/storage/rowset/segment_iterator.cpp` — dispatches to ACORN / Grid-HNSW readers

## Deferred Work

- **Boundary repair**: cross-partition stitching for Grid-HNSW border vectors
- **Cost model**: auto-select ACORN vs Grid-HNSW based on selectivity and spatial-vector correlation
- **Cosine / IP metrics**: currently only `l2_distance` is supported for spatial modes
- **Dynamic rho estimation**: per-segment correlation estimation at compaction time

## Getting Started

See [PRODUCTION_GUIDE.md](PRODUCTION_GUIDE.md) for the complete deployment and usage guide.

Quick benchmark:

```bash
cd benchmarks/
./run_full_benchmark.sh --skip-load --skip-baseline --only acorn
```
