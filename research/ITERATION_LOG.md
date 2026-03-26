# Iteration Log

This document tracks the self-growing research plan. Each iteration follows the pattern:
**Gather → Synthesize → Reflect → Plan Next**.

---

## Iteration 0: Bootstrap

**Date**: 2025-03-20  
**Goal**: Establish baseline understanding of StarRocks capabilities and the problem space.

### What We Gathered

**StarRocks Vector Search (experimental)**:
- HNSW and IVFPQ implemented via TenANN library (`be/src/storage/index/vector/`)
- Data type: `ARRAY<FLOAT>`, distance functions: `approx_l2_distance`, `approx_cosine_similarity`
- Optimizer rule `RewriteToVectorPlanRule` transforms TopN+Scan into ANN scan
- Per-segment index, built during flush when row count exceeds threshold (default 100)
- Gated by `enable_experimental_vector` (default: false)
- Platform: Linux x86_64/arm64 only (TenANN dependency)

**StarRocks Spatial/GIS (basic)**:
- ~15 functions: `ST_Contains`, `ST_Point`, `ST_Distance_Sphere`, `ST_GeometryFromText`, etc.
- Internal geometry via Google S2 library (`GeoPoint`, `GeoLine`, `GeoPolygon`, `GeoCircle`)
- No native GEOMETRY type — stored as binary in VARCHAR
- No spatial indexes, no spatial predicate pushdown, no spatial query optimization

**StarRocks Index Infrastructure**:
- Supported: BITMAP, GIN, NGRAMBF, VECTOR
- Factory/plugin patterns for extensibility
- Per-segment index files (`.vi` for vector, `.ivt` for GIN)
- Clear extension path: proto enum → grammar → FE validation → BE writer/reader → scan integration

### Key Insight

StarRocks has both S2 (spatial) and TenANN (vector) libraries but they operate in completely separate code paths with no interaction. The spatial functions are pure computation (no indexing), while vector indexes have no awareness of spatial predicates. A hybrid index must bridge these two worlds.

### Assumptions

1. The primary query pattern is spatial-filter + K-ANN (spatial predicate first, then vector similarity)
2. The reverse order (ANN first, then spatial filter) is less efficient due to required oversampling
3. The hybrid index should be a per-segment structure, consistent with StarRocks' storage architecture
4. S2 cells can serve as spatial partitioning keys within the index

### Questions for Next Iteration

1. What academic work exists on filtered/constrained ANN with spatial predicates?
2. How do production vector databases (Qdrant, Milvus, Weaviate) handle geo+ANN queries?
3. What is the state of the art for predicate-aware graph traversal in HNSW?
4. Can existing filtered ANN techniques (Filtered-DiskANN, ACORN) be adapted for spatial predicates?

### Next Steps

→ **Iteration 1**: Literature survey on filtered ANN and spatial+vector hybrid indexes  
→ **Iteration 2**: Industry survey of production systems

---

## Iteration 1: Literature Survey

**Date**: 2025-03-20  
**Goal**: Map the academic landscape of filtered/constrained ANN and spatial+vector hybrid indexes.

### What We Gathered

**Filtered ANN (General)**:
- **ACORN** (Stanford, 2024): Predicate-agnostic filtered HNSW. Introduces "predicate subgraph traversal" — explores neighbors-of-neighbors to bypass filtered-out nodes. 2-1000x higher throughput at fixed recall vs prior methods. Adopted by Apache Lucene 10.2.0 (Feb 2025).
- **Filtered-DiskANN** (Microsoft, WWW 2023): Graph-based indices that incorporate label metadata during construction. Order of magnitude faster for filtered queries. Supports streaming updates.
- **Compass** (2024): General filtered search across vector+structured data. Uses cooperative execution between HNSW/IVF and B+-trees. Outperforms NaVix on diverse workloads.
- **NaviX** (2024): Native vector index for graph DBMSs with predicate-agnostic search. Designed for systems where the HNSW graph is deeply integrated with the DBMS.

**Spatial-Specific ANN**:
- **Mesh** (VLDB Journal, Jan 2025): Memory-efficient index for top-k spatial-range-constrained ANN (k-RANNS) on geo-tagged vectors. Workload-aware construction with adaptive query strategies based on selectivity. Solves combinatorial optimization for index construction.
- **LIST** (VLDB Journal, 2024): Learning-based index for spatio-textual data. Uses learning-to-cluster to group spatially and textually relevant objects. Pseudo-label generation for training.
- **KHI** (2024): Combines HNSW graphs with attribute-space partitioning trees. 16.22x throughput improvement for multi-attribute range filtering on high-dimensional vectors.
- **IVF²** (2025): Fuses classical inverted indices with spatial inverted indices for filtered ANNS with binary metadata constraints.

**Range-Filtering ANN** (closely related to spatial range queries):
- **UNIFY** (2024): Unified proximity graph supporting pre-, post-, and hybrid filtering via Segmented Inclusive Graph (SIG) and Hierarchical SIG.
- **Dynamic Segment Graph** (VLDB 2025): Handles dynamic range-filtered ANN where vectors arrive in arbitrary order. Compresses multiple HNSW graphs.
- **WoW** (2025): Window-to-window incremental index for range-filtering ANN. 4x faster queries than other incremental indices.

### Key Insights

1. **ACORN is the current SOTA for general filtered HNSW** — it's predicate-agnostic, meaning spatial predicates work without special handling. Already production-proven in Lucene/Elasticsearch.

2. **Mesh is the most directly relevant paper** — it addresses exactly our problem (geo-tagged vectors with spatial range constraints) and provides a workload-aware, memory-efficient solution.

3. **The filtered ANN field is very active (2024-2025)** — multiple papers from VLDB, SIGMOD, WWW. This validates that the problem is important and unsolved.

4. **Two architectural patterns emerge**:
   - **Graph-modification**: Modify HNSW traversal to skip filtered nodes (ACORN, Filtered-DiskANN)
   - **Partition-based**: Spatial partitioning with per-partition vector indexes (Mesh, KHI, spatial-IVF)

5. **Selectivity-adaptive strategies are critical** — all recent papers emphasize adapting the query strategy based on filter selectivity (what fraction of data passes the spatial filter).

### Revised Assumptions

- Assumption 4 is validated: S2 cells can partition the space, and per-partition HNSW is a viable approach (supported by Mesh, KHI)
- New assumption: A cost model based on spatial selectivity should drive strategy selection (pre-filter vs. modified traversal vs. brute force)

### Questions for Next Iteration

1. How do production vector databases implement these ideas at scale?
2. What are the trade-offs between ACORN-style graph modification and Mesh-style spatial partitioning?
3. How does filter selectivity affect the optimal strategy in practice?
4. Can we combine partition-based and graph-modification approaches?

### Next Steps

→ **Iteration 2**: Industry survey — how do Qdrant, Elasticsearch, Vespa, etc. implement filtered vector search with geo constraints?  
→ Revisit `03_algorithmic_design/` approach taxonomy based on the two architectural patterns identified

---

## Iteration 2: Industry Survey

**Date**: 2025-03-20  
**Goal**: Document how production systems handle spatial+vector queries.

### What We Gathered

| System | Approach | Geo Support | Key Insight |
|--------|----------|-------------|-------------|
| **Qdrant** | Filterable HNSW with payload indexes | Geo radius, bbox, polygon | Builds filter-aware HNSW subgraphs per payload value; query planner adapts strategy by filter cardinality |
| **Elasticsearch** | HNSW + geo pre-filter | geo_shape, geo_point, geo_distance | ACORN-1 adopted in Lucene 10.2.0 (Feb 2025) for filtered HNSW; geo_shape filters combined with kNN |
| **Vespa** | HNSW + geoLocation clause | Point + distance filters | Pre-filtering optimization (was post-filter only); AND combination in YQL |
| **Weaviate** | nearVector + WithinGeoRange | Geo coordinates + range filter | Geo filter as where clause on vector search; implementation details sparse |
| **Milvus** | Partition key + scalar filtering | No native geo type | Partition-based optimization; scalar filter expressions on vector search |
| **Pinecone** | Metadata filtering | No geo support (feature request) | MongoDB-style filter operators on metadata; geo is a requested but unimplemented feature |
| **PostGIS+pgvector** | Separate extensions, combined SQL | Full PostGIS spatial support | No unified index; separate GiST (spatial) + HNSW (vector) indexes |
| **DuckDB** | Separate extensions | Spatial extension + VSS extension | No combined index; HNSW for vectors, spatial ops separate |

### Key Insights

1. **No production system has a truly hybrid spatial+vector index.** All use some form of filtering (pre or post) on top of separate index structures. This is a genuine opportunity.

2. **Qdrant's Filterable HNSW is the most sophisticated production approach** — it builds filter-aware HNSW subgraphs and has a query planner that adapts based on filter selectivity.

3. **Elasticsearch/Lucene adopted ACORN-1** — validating the academic approach at scale. The key insight is adaptive: use ACORN for selective filters, bypass for permissive ones.

4. **PostGIS+pgvector is the closest to our use case** but has no index-level integration — queries run against separate GiST and HNSW indexes and results are combined at the query level.

5. **Vespa's pre-filter challenge is instructive** — they initially applied geo filters as post-filters on ANN results, which was inefficient. Pre-filtering required explicit engineering.

6. **All systems struggle with the selectivity problem**: highly selective spatial filters make ANN search on filtered subsets too small for graph traversal, while permissive filters waste compute on post-filtering.

### Revised Approach Rankings

Based on industry validation:
1. **Filterable HNSW with spatial predicates** (ACORN-style) — proven at scale by Elasticsearch/Lucene
2. **Spatial-partitioned HNSW** (Qdrant-style subgraphs) — production-proven but not spatial-specific
3. **Cascade pipeline with cost model** — simple, builds on existing StarRocks infrastructure
4. **R-tree + HNSW hybrid** — no production systems use this, but has academic backing (Mesh)

### Questions for Next Iteration

1. Given StarRocks' segment-based architecture, which approach maps best?
2. Can we combine ACORN-style traversal with S2-based spatial partitioning?
3. What would a cost model look like that selects between strategies at query time?

### Next Steps

→ **Iteration 3**: Algorithmic design — detail each candidate approach  
→ **Iteration 4**: StarRocks-specific design — map the best approach to SR's architecture  
→ **Iteration 5**: Evaluation framework

---

## Refinement: Spatial–Vector Correlation (ρ)

**Date**: 2026-03-20  
**Goal**: Extend the design space with **positive / zero / negative** correlation between **spatial proximity** and **embedding similarity**, and map **which hybrid index strategy** is strongest in each regime.

### What We Gathered

- **Positive ρ**: Geographic neighbors tend to be close in vector space → spatial partitions **align** with vector neighborhoods; **spatial-first** and **S2-partitioned HNSW** are favored; **vector-first** needs **less** oversampling.
- **ρ ≈ 0**: Location and embeddings are **independent** → **no** structural bonus from space in the HNSW graph; **filtered ANN (ACORN)** and **selectivity** drive the plan; **vector-first** requires **~1/σ_s** oversampling (worst case).
- **Negative ρ**: Neighbors on the map are often **far** in embedding space → **partition boundaries** can separate true k-NN; **cross-partition stitching**, **higher efSearch / 2-hop**, and **avoiding naive vector-first**; **hybrid graph** (spatial edges) is most justified here.

### Key Insights

1. **Selectivity (σ_s) and correlation (ρ) are orthogonal knobs** — both belong in the **cost model** and **benchmarks**.
2. **Spatial-partitioned HNSW** is **not** universally optimal: it is **strongest for ρ ≥ 0** and **riskiest for ρ ≪ 0** without **stitching**.
3. **LIST / KHI**-style work already assumes **joint** structure (learned or attribute-space partitioning) — aligns with **positive ρ** or **explicit** joint models.

### Revised Assumptions

- A single “best hybrid index” may **not** exist; **StarRocks** should aim for a **tiered** design plus **ρ̂** (estimated correlation) or **user hints** to choose **partitioned vs. filtered** and **stitching on/off**.

### Questions for Next Iteration

1. How can we **estimate ρ̂** per **segment** cheaply at **compaction** time?
2. What **benchmark suites** fix **σ_s** while **sweeping ρ** (synthetic generators)?

### Next Steps

→ Update [03_algorithmic_design/spatial_vector_correlation.md](03_algorithmic_design/spatial_vector_correlation.md), [cost_model.md](03_algorithmic_design/cost_model.md), [05_evaluation/datasets.md](05_evaluation/datasets.md)  
→ Prototype **ρ-sweep** workloads before locking **index** defaults

---

## Template for Future Iterations

```markdown
## Iteration N: [Title]

**Date**: YYYY-MM-DD
**Goal**: [One sentence]

### What We Gathered
[Findings, data, papers, code reviewed]

### Key Insights
[Numbered list of insights]

### Revised Assumptions
[What changed from prior iterations]

### Questions for Next Iteration
[What we still don't know]

### Next Steps
→ [Concrete action items]
```
