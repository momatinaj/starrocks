# Architecture Fit

## StarRocks Storage Architecture Overview

```
Table
├── Partition (by date, range, or hash)
│   ├── Tablet (horizontal shard)
│   │   ├── Rowset (immutable data batch)
│   │   │   ├── Segment 0 (.dat)
│   │   │   │   ├── Column data (columnar storage)
│   │   │   │   ├── Zone maps (min/max per page)
│   │   │   │   ├── Bitmap index (.bmp)
│   │   │   │   ├── Bloom filter index
│   │   │   │   ├── GIN index (.ivt)
│   │   │   │   └── Vector index (.vi)
│   │   │   ├── Segment 1 (.dat)
│   │   │   └── ...
│   │   └── ...
│   └── ...
└── ...
```

## Where the Hybrid Index Fits

### Level 1: Table Partitioning (Coarse Spatial Pruning)

Users can partition tables by S2 cell level to achieve coarse spatial data locality:

```sql
CREATE TABLE geo_vectors (
    id BIGINT,
    location VARCHAR,
    s2_cell_id BIGINT,
    embedding ARRAY<FLOAT>,
    ...
) PARTITION BY RANGE(s2_cell_id) (
    PARTITION p_cell_0 VALUES [("0"), ("1000000000")),
    PARTITION p_cell_1 VALUES [("1000000000"), ("2000000000")),
    ...
);
```

This provides tablet-level spatial pruning but is too coarse for fine-grained spatial queries. It is complementary to the hybrid index, not a replacement.

### Level 2: Per-Segment Hybrid Index (Our Target)

The hybrid index lives at the segment level, alongside existing indexes:

```
Segment
├── Column data (location, embedding, other columns)
├── Existing indexes (zone map, bitmap, bloom filter)
├── Existing vector index (.vi) — may coexist or be replaced
└── Hybrid spatial+vector index (.hvi)  ← NEW
    ├── S2 Cell Index (cell → partition mapping)
    ├── Per-partition HNSW graphs
    └── Spatial metadata (cell statistics, row locations)
```

### Level 3: Query Planner Integration

```
SQL Query
  → FE Parser (recognizes spatial+vector pattern)
  → FE Optimizer (RewriteToHybridVectorPlanRule)
    → Selectivity estimation
    → Strategy selection
  → BE Segment Scan
    → HybridIndexReader
      → S2 Cell resolution
      → HNSW search (per-partition or filtered)
    → Result merging across segments
  → Top-K aggregation
  → Result
```

## Alignment with Existing Infrastructure

### What We Reuse

| Component | Existing | How We Use It |
|-----------|----------|---------------|
| S2 Library | `be/src/geo/geo_types.h` | S2 cell computation, region covering, containment checks |
| TenANN HNSW | `be/src/storage/index/vector/tenann/` | Per-partition HNSW build and search |
| Vector Search Options | `VectorSearchOptions` | Extended with spatial parameters |
| Index Descriptor | `be/src/storage/index/index_descriptor.h` | File path management for .hvi files |
| Segment Writer | `be/src/storage/rowset/segment_writer.cpp` | Trigger hybrid index build during flush |
| Optimizer Rules | `RewriteToVectorPlanRule.java` | Extended for spatial predicate awareness |
| Del ID Filter | `tenann/del_id_filter.h` | Row ID filtering in HNSW search (Tier 0) |
| Column Writer | `column_writer.cpp` | Passes data to hybrid index builder |

### What We Build New

| Component | Purpose |
|-----------|---------|
| `HybridIndexBuilder` | Builds S2-partitioned HNSW per segment |
| `HybridIndexReader` | Reads and queries hybrid index |
| `HybridIndexWriter` | Writes .hvi file format |
| `S2CellIndex` | Cell → partition mapping structure |
| `SpatialSelectivityEstimator` | Estimates σ_s from cell statistics |
| `RewriteToHybridVectorPlanRule` | FE optimizer rule (extends existing) |

## Segment Lifecycle Alignment

### Write Path

```
Data Ingestion
  → MemTable accumulation
  → Flush to Segment
    → ColumnWriter writes location + embedding columns
    → HybridIndexBuilder receives (row_id, location, embedding) tuples
      → Computes S2 cell IDs
      → Groups by cell
      → Builds per-partition HNSW
      → Writes .hvi file
```

### Read Path

```
Query arrives
  → Segment Iterator initialized with HybridSearchOptions
  → HybridIndexReader opened
    → Reads S2 Cell Index header
    → Resolves spatial query to cell set
    → Opens relevant partition HNSW readers
  → Search each partition
  → Merge and return top-k row IDs + distances
  → Segment Iterator fetches remaining columns for returned rows
```

### Compaction Path

```
Compaction merges segments
  → Source segments' hybrid indexes are read
  → Merged data is re-partitioned by S2 cells
  → New hybrid index is built for merged segment
  → Old segments' .hvi files are deleted
```

This is the same pattern as existing vector index compaction — rebuild during merge.

## Shared-Data Mode Considerations

In shared-data mode:
- Segment files are stored on object storage (S3, HDFS)
- Index files (.hvi) would be stored alongside segment files
- Block cache can cache hot S2 Cell Index headers and frequently accessed partitions
- Configuration: `enable_vector_index_block_cache` (existing) can be extended for hybrid index

## Constraints and Risks

| Constraint | Impact | Mitigation |
|------------|--------|------------|
| TenANN Linux-only | No macOS development/testing of real ANN | Fallback to brute-force (existing pattern via EmptyIndexReader) |
| S2 cell level is fixed at build time | Cannot change granularity without rebuild | Use multiple levels, or adaptive level selection |
| Per-segment index | Cannot do cross-segment graph traversal | Merge results across segments at query level (existing pattern) |
| Immutable segments | Cannot update index after write | Rebuild during compaction (existing pattern) |
| Memory for multiple HNSW graphs | More memory than single HNSW | Quantization, lazy loading of partitions |
