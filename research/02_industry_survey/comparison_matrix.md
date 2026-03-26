# Comparison Matrix

## Feature Comparison

| Feature | Qdrant | Elasticsearch | Vespa | Weaviate | Milvus | Pinecone | PostGIS+pgvector | DuckDB |
|---------|--------|--------------|-------|----------|--------|----------|-----------------|--------|
| **Geo Data Type** | Payload (lat/lon) | geo_point, geo_shape | position | geoCoordinates | None (scalar) | None (scalar) | geometry, geography | Geometry (GEOS) |
| **Geo Predicates** | Radius, BBox, Polygon | Full OGC | Point+radius | Point+radius | Range on scalars | Range on scalars | Full OGC SFS | Full OGC (GEOS) |
| **Vector Index** | HNSW | HNSW (Lucene) | HNSW | HNSW | HNSW,IVF,DiskANN | Proprietary | HNSW, IVFFlat | HNSW |
| **Filtered HNSW** | Filterable HNSW | ACORN-1 | Global filter | Basic | Scalar filter | Metadata filter | None | None |
| **Spatial Index** | Payload index | BKD tree | Position index | None | None | None | GiST (R-tree) | R-tree (exp.) |
| **Pre-filter Geo** | Yes (planner) | Yes (ACORN-1) | Yes (recent fix) | Unclear | Via expressions | Via metadata | Via planner | No |
| **Selectivity Adaptation** | Yes | Yes (8/40%) | No | No | No | No | Planner-based | No |
| **Hybrid Index** | No | No | No | No | No | No | No | No |
| **SQL Interface** | No | ES\|QL | YQL | GraphQL | PyMilvus | Python SDK | Full SQL | Full SQL |
| **OLAP Optimized** | No | No | No | No | No | No | No | Yes |
| **Open Source** | Yes | Yes (core) | Yes | Yes | Yes | No | Yes | Yes |

## Strategy Comparison

| Strategy | Systems Using It | Pros | Cons |
|----------|-----------------|------|------|
| **Post-filter** | Vespa (historical), basic implementations | Simple, no index changes | Wastes ANN search budget on irrelevant results |
| **Pre-filter + brute-force** | PostGIS+pgvector (planner choice) | Exact results for selective filters | No ANN acceleration on filtered subset |
| **Filtered graph traversal** | Qdrant, Elasticsearch, Vespa (current) | Combines filtering and ANN in single pass | Graph connectivity degrades with selective filters |
| **Partition-based** | Milvus (partition key) | Good data locality, scalable | Coarse granularity, cross-partition overhead |
| **Separate indexes** | PostGIS+pgvector, DuckDB | Uses best-of-breed for each | No coordination, planner must choose order |

## Gap Analysis

**What no system does**:
1. Spatially-aware HNSW construction (edges biased toward spatial neighbors)
2. S2/H3 cell hierarchy integrated into HNSW layers
3. Cost model specifically for spatial selectivity + ANN interaction
4. Per-segment hybrid indexes in a columnar OLAP engine
5. Spatial-partitioned HNSW with cross-partition stitching

**Opportunity for StarRocks**:
A hybrid index that combines S2-based spatial partitioning with HNSW graph structure within StarRocks' per-segment architecture would be unique in both academia and industry. The closest approaches are:
- Mesh (academic, not in any production system)
- Qdrant's filterable HNSW (production, but not spatial-hierarchy-aware)
- Elasticsearch's ACORN-1 (production, but treats spatial as generic predicate)

## Maturity Ranking (for Geo+Vector specifically)

1. **Elasticsearch** — ACORN-1 + rich geo support, most mature
2. **Qdrant** — Filterable HNSW + geo payloads, most sophisticated architecture
3. **PostGIS+pgvector** — Best spatial support, no index integration
4. **Vespa** — Good ANN, basic geo with recent pre-filter fix
5. **Weaviate** — Basic geo, limited spatial predicates
6. **Milvus** — No native geo, partition-based workaround
7. **DuckDB** — Both extensions exist, no integration
8. **Pinecone** — No geo support
