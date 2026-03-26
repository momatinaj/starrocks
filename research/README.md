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

## Why a Hybrid Index?

Naive approaches have fundamental performance problems:

| Approach | How it works | Problem |
|----------|-------------|---------|
| **Spatial-first** | Full spatial scan → brute-force ANN on survivors | ANN degrades to linear scan on large spatial result sets |
| **Vector-first** | ANN returns K candidates → spatial filter | Must oversample K by 10-100x to get K results after filtering |
| **Separate indexes** | Independent spatial index + vector index | No cross-optimization; results must be intersected post-hoc |

A hybrid index co-locates spatial and vector information so a single index traversal can satisfy both predicates efficiently.

**Spatial–vector correlation:** Real datasets differ in whether **nearby rows are similar in embedding space** (positive ρ), **unrelated** (ρ ≈ 0), or **diverse nearby** (negative ρ). That choice affects **vector-first oversampling**, **spatial-partitioned HNSW** (boundary cuts), and **filtered ANN** tuning. See [03_algorithmic_design/spatial_vector_correlation.md](03_algorithmic_design/spatial_vector_correlation.md).

## Repository Structure

```
research/
├── README.md                          ← You are here
├── ITERATION_LOG.md                   # Self-growing plan: findings → next steps
│
├── 01_literature_survey/              # Academic landscape
│   ├── README.md                      # Survey methodology and summary
│   ├── spatial_indexes.md             # R-tree, R*-tree, KD-tree, S2, H3, geohash
│   ├── vector_indexes.md              # HNSW, IVF, DiskANN, ScaNN, SPANN
│   ├── hybrid_approaches.md           # Papers combining spatial + vector
│   ├── filtered_ann.md                # Filtered/constrained ANN literature
│   └── papers/                        # BibTeX references and detailed paper notes
│       ├── bibliography.bib
│       └── paper_notes/
│           └── template.md
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
├── 04_starrocks_design/               # StarRocks-specific integration
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
└── 05_evaluation/                     # Benchmarking and validation
    ├── README.md                      # Evaluation framework
    ├── datasets.md                    # GIS+vector datasets
    ├── metrics.md                     # Recall@K, QPS, latency, memory
    ├── test_queries.md                # Representative query patterns
    └── baseline_results.md            # Results from naive approaches
```

## StarRocks Baseline

| Capability | Status | Key Files |
|-----------|--------|-----------|
| **Vector Search** | Experimental (HNSW, IVFPQ via TenANN) | `be/src/storage/index/vector/` |
| **Spatial Functions** | Basic (~15 functions via S2) | `be/src/exprs/geo_functions.cpp` |
| **Spatial Indexes** | None | — |
| **Index Infrastructure** | Extensible (BITMAP, GIN, NGRAMBF, VECTOR) | `gensrc/proto/types.proto` |

## Research Methodology

This research follows an **iterative, self-growing plan**:

1. **Gather** information (papers, systems, code)
2. **Synthesize** findings into the relevant survey document
3. **Update** the iteration log with new insights and revised priorities
4. **Plan** the next iteration based on what was learned
5. **Repeat** until a concrete, validated design emerges

All iterations are tracked in [ITERATION_LOG.md](ITERATION_LOG.md).

## Quick Navigation

| Phase | Status | Entry Point |
|-------|--------|-------------|
| Literature Survey | In Progress | [01_literature_survey/README.md](01_literature_survey/README.md) |
| Industry Survey | In Progress | [02_industry_survey/README.md](02_industry_survey/README.md) |
| Algorithmic Design | Planned | [03_algorithmic_design/README.md](03_algorithmic_design/README.md) |
| StarRocks Design | Planned | [04_starrocks_design/README.md](04_starrocks_design/README.md) |
| Evaluation | Planned | [05_evaluation/README.md](05_evaluation/README.md) |
