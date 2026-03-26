# Industry Survey

## Scope

This survey documents how production databases and vector search systems handle queries combining spatial (geographic) filtering with vector similarity search.

## Methodology

- Reviewed official documentation, blog posts, GitHub issues/PRs, and architecture guides
- Focused on: how geo filters interact with ANN indexes, pre-filter vs post-filter, query planning
- Tested claims against documentation where possible

## Systems Surveyed

| System | Type | Geo+Vector Support | Detail |
|--------|------|-------------------|--------|
| [Qdrant](qdrant.md) | Vector DB | Native geo filters + filterable HNSW | Most sophisticated |
| [Elasticsearch](elasticsearch.md) | Search engine | ACORN-1 filtered HNSW + geo_shape | Production-proven at scale |
| [Vespa](vespa.md) | Search platform | nearestNeighbor + geoLocation | Pre-filter optimization |
| [Weaviate](weaviate.md) | Vector DB | WithinGeoRange + nearVector | Basic geo filtering |
| [Milvus](milvus.md) | Vector DB | Partition key + scalar filtering | No native geo, partition-based |
| [Pinecone](pinecone.md) | Vector DB (managed) | Metadata filtering only | No geo support |
| [PostGIS+pgvector](postgis_pgvector.md) | RDBMS extensions | Separate GiST + HNSW indexes | SQL-level combination |
| [DuckDB](duckdb.md) | OLAP engine | Separate spatial + VSS extensions | No integration |

## Key Finding

**No production system has a truly hybrid spatial+vector index.** All use one of these patterns:

1. **Post-filtering**: Run ANN search, then filter results by spatial predicate (wasteful for selective spatial queries)
2. **Pre-filtering**: Apply spatial filter first, then brute-force ANN on survivors (wasteful for large spatial result sets)
3. **Filtered graph traversal**: Modify HNSW traversal to skip non-matching nodes (ACORN in Elasticsearch, filterable HNSW in Qdrant)
4. **Separate indexes**: Maintain independent spatial and vector indexes, combine at query level (PostGIS+pgvector)

Pattern 3 is the most advanced production approach, but it still treats spatial as a generic predicate — no spatial-specific optimization.

## Comparison Matrix

See [comparison_matrix.md](comparison_matrix.md) for a detailed feature comparison.

## Navigation

- [Qdrant](qdrant.md) — Filterable HNSW with geo payloads
- [Elasticsearch](elasticsearch.md) — ACORN-1 + geo_shape
- [Vespa](vespa.md) — ANN + geoLocation
- [Weaviate](weaviate.md) — nearVector + WithinGeoRange
- [Milvus](milvus.md) — Partition-based filtering
- [Pinecone](pinecone.md) — Metadata filtering (no geo)
- [PostGIS+pgvector](postgis_pgvector.md) — PostgreSQL extension stacking
- [DuckDB](duckdb.md) — OLAP extensions
- [Comparison Matrix](comparison_matrix.md)
