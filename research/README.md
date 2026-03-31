# Hybrid GIS+Vector Index Research

## Objective

Design and implement a **hybrid index** for StarRocks that efficiently combines spatial (GIS) filtering with approximate nearest neighbor (ANN) vector search. The target query pattern is:

```sql
SELECT *, approx_l2_distance(embedding, [0.1, 0.2, ...]) AS dist
FROM signs
WHERE ST_Contains(ST_Polygon('POLYGON((...))'), location)
ORDER BY dist
LIMIT 10;
```

This pattern appears in applications like visual geo-search ("find all red signs in this area"), where a spatial predicate restricts the geographic scope and a vector similarity search finds semantically matching items within that scope.

## Current Status

Three spatial-vector index types have been implemented:

| Index Type | Branch | Description | Status |
|------------|--------|-------------|--------|
| **ACORN-1** | `feature/spatial-vector-fallback` | Predicate-aware HNSW traversal with 2-hop expansion | Shipped |
| **Grid-HNSW** | `feature/spatial-vector-fallback` | S2-partitioned per-cell HNSW indexes | Shipped |
| **ACORN-gamma** | `feature/acorn-gamma` | Dense-graph HNSW (gamma*M neighbors) + ACORN search | In development |

ACORN-gamma builds a denser graph at construction time (M_effective = M * gamma, default gamma=2) while reusing the same ACORN predicate-aware 2-hop search. The denser graph improves recall under selective spatial predicates.

See [06_s2_hnsw_execution_program/PRODUCTION_GUIDE.md](06_s2_hnsw_execution_program/PRODUCTION_GUIDE.md) for deployment, usage, and benchmarking instructions.

## Why a Hybrid Index?

Naive approaches have fundamental performance problems:

| Approach | How it works | Problem |
|----------|-------------|---------|
| **Spatial-first** | Full spatial scan, brute-force ANN on survivors | ANN degrades to linear scan on large spatial result sets |
| **Vector-first** | ANN returns K candidates, spatial filter | Must oversample K by 10-100x to get K results after filtering |
| **Separate indexes** | Independent spatial index + vector index | No cross-optimization; results must be intersected post-hoc |

A hybrid index co-locates spatial and vector information so a single index traversal can satisfy both predicates efficiently.

**Spatial-vector correlation:** Real datasets differ in whether **nearby rows are similar in embedding space** (positive rho), **unrelated** (rho near 0), or **diverse nearby** (negative rho). That choice affects **vector-first oversampling**, **spatial-partitioned HNSW** (boundary cuts), and **filtered ANN** tuning. See [03_algorithmic_design/spatial_vector_correlation.md](03_algorithmic_design/spatial_vector_correlation.md).

## Repository Structure

```
research/
├── README.md                          ← You are here
├── ITERATION_LOG.md                   # Research iteration history
│
├── 01_literature_survey/              # Academic landscape
│   ├── README.md                      # Survey methodology and summary
│   ├── spatial_indexes.md             # R-tree, R*-tree, KD-tree, S2, H3, geohash
│   ├── vector_indexes.md              # HNSW, IVF, DiskANN, ScaNN, SPANN
│   ├── hybrid_approaches.md           # Papers combining spatial + vector
│   ├── filtered_ann.md                # Filtered/constrained ANN literature
│   └── papers/
│       └── bibliography.bib
│
├── 02_industry_survey/                # Production systems survey
│   ├── README.md                      # Survey methodology and summary
│   ├── postgis_pgvector.md            # PostGIS + pgvector integration
│   ├── milvus.md                      # Milvus filtered search / partitioning
│   ├── weaviate.md                    # Weaviate geo filters
│   ├── qdrant.md                      # Qdrant payload filtering
│   ├── elasticsearch.md               # ES dense_vector + geo_shape
│   ├── pinecone.md                    # Pinecone metadata filtering
│   ├── vespa.md                       # Vespa geo + ANN
│   ├── duckdb.md                      # DuckDB spatial + VSS extensions
│   └── comparison_matrix.md           # Feature comparison across systems
│
├── 03_algorithmic_design/             # Candidate hybrid index designs
│   ├── README.md                      # Design space overview
│   ├── approach_taxonomy.md           # Classification of approaches
│   ├── spatial_partitioned_hnsw.md    # HNSW per spatial partition (S2/H3)
│   ├── r_tree_plus_hnsw.md            # R-tree with HNSW at leaf nodes
│   ├── filtered_hnsw.md               # Predicate-aware HNSW traversal
│   ├── hybrid_graph.md                # Single graph with spatial+vector edges
│   ├── cascade_pipeline.md            # Spatial filter → ANN pipeline
│   ├── cost_model.md                  # Selectivity + correlation-aware strategy
│   ├── spatial_vector_correlation.md  # Positive / zero / negative ρ → best index
│   └── benchmarks/
│       ├── workload_definition.md     # Test queries, datasets, metrics
│       └── expected_baselines.md      # Expected perf of naive approaches
│
├── 04_starrocks_design/               # StarRocks-specific integration design
│   ├── README.md                      # Design overview
│   ├── architecture_fit.md            # How hybrid index fits SR architecture
│   ├── index_type_design.md           # New IndexType, storage format
│   ├── query_planner_changes.md       # FE optimizer rule modifications
│   ├── segment_integration.md         # BE segment writer/reader changes
│   ├── sql_syntax.md                  # CREATE INDEX syntax for hybrid
│   ├── s2_integration.md              # Leveraging existing S2 library
│   ├── tenann_extensions.md           # Extending TenANN or alternatives
│   └── implementation_roadmap.md      # Phased implementation plan
│
├── 05_evaluation/                     # Benchmarking and validation
│   ├── README.md                      # Evaluation framework
│   ├── datasets.md                    # GIS+vector datasets
│   ├── metrics.md                     # Recall@K, QPS, latency, memory
│   ├── test_queries.md                # Representative query patterns
│   └── baseline_results.md            # Results from naive approaches
│
└── 06_s2_hnsw_execution_program/      # Implementation + benchmarks
    ├── README.md                      # Program overview and status
    ├── PRODUCTION_GUIDE.md            # ★ Deployment, usage, benchmarking guide
    ├── MASTER_PLAN.md                 # Roadmap and phase tracking
    ├── ABLATION_MATRIX.md             # Benchmark configuration matrix
    ├── RISKS_AND_ASSUMPTIONS.md       # Risk register
    └── benchmarks/                    # Benchmark scripts and results
        ├── README.md                  # Quick-start for running benchmarks
        ├── run_benchmark.py           # Main benchmark runner
        ├── run_full_benchmark.sh      # One-command full comparison
        ├── compare_results.py         # Side-by-side text comparison
        ├── generate_report.py         # HTML report with charts
        └── rebuild_and_benchmark.sh   # CI-style build + bench pipeline
```

## Quick Navigation

| Phase | Status | Entry Point |
|-------|--------|-------------|
| Literature Survey | Complete | [01_literature_survey/README.md](01_literature_survey/README.md) |
| Industry Survey | Complete | [02_industry_survey/README.md](02_industry_survey/README.md) |
| Algorithmic Design | Complete | [03_algorithmic_design/README.md](03_algorithmic_design/README.md) |
| StarRocks Design | Complete | [04_starrocks_design/README.md](04_starrocks_design/README.md) |
| Evaluation Framework | Complete | [05_evaluation/README.md](05_evaluation/README.md) |
| Implementation | **Shipped** | [06_s2_hnsw_execution_program/PRODUCTION_GUIDE.md](06_s2_hnsw_execution_program/PRODUCTION_GUIDE.md) |
