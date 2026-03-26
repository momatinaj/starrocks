# Segment Integration

## Overview

This document details the BE-side changes needed to integrate the hybrid spatial+vector index into StarRocks' segment writer/reader pipeline.

## New Files

```
be/src/storage/index/hybrid_vector/
├── hybrid_vector_index_builder.h
├── hybrid_vector_index_builder.cpp
├── hybrid_vector_index_reader.h
├── hybrid_vector_index_reader.cpp
├── hybrid_vector_index_writer.h
├── hybrid_vector_index_writer.cpp
├── s2_cell_index.h
├── s2_cell_index.cpp
├── spatial_partition.h
├── spatial_partition.cpp
└── hybrid_search_option.h
```

## Segment Writer Integration

### ColumnWriterOptions Extension

In `be/src/storage/rowset/column_writer.h`:
```cpp
struct ColumnWriterOptions {
    // ... existing fields ...
    bool need_hybrid_vector_index = false;
    std::shared_ptr<TabletIndex> hybrid_vector_tablet_index;
};
```

### Segment Writer Changes

In `be/src/storage/rowset/segment_writer.cpp`:

```cpp
// In _init_column_writers():
// Check if column has SPATIAL_VECTOR index
if (tablet_schema.has_index(column.unique_id(), SPATIAL_VECTOR)) {
    opts.need_hybrid_vector_index = true;
    opts.hybrid_vector_tablet_index = 
        tablet_schema.get_index_by_type(column.unique_id(), SPATIAL_VECTOR);
}
```

### HybridVectorIndexBuilder

```cpp
class HybridVectorIndexBuilder {
public:
    HybridVectorIndexBuilder(
        const std::shared_ptr<TabletIndex>& tablet_index,
        const std::string& index_file_path);
    
    // Called for each row during segment write
    Status add(uint32_t row_id, 
               const float* embedding, uint32_t dim,
               double lat, double lon);
    
    // Called at segment finalization
    Status finish();
    
private:
    // S2 cell computation
    S2CellId compute_cell_id(double lat, double lon) const;
    
    // Partition management
    struct Partition {
        S2CellId cell_id;
        std::vector<uint32_t> row_ids;
        std::vector<std::vector<float>> embeddings;
        std::vector<std::pair<double, double>> locations;
    };
    std::map<S2CellId, Partition> _partitions;
    
    // Build per-partition HNSW using TenANN
    Status build_partition_hnsw(const Partition& partition);
    
    // Write .hvi file
    Status write_index_file();
    
    int _s2_level;
    int _min_partition_size;
    uint32_t _dim;
    std::string _index_file_path;
};
```

## Segment Reader Integration

### HybridVectorIndexReader

```cpp
class HybridVectorIndexReader {
public:
    // Open and read .hvi file header
    Status open(const std::string& index_file_path);
    
    // Main search interface
    Status search(
        const HybridSearchOption& option,
        std::vector<uint32_t>* result_row_ids,
        std::vector<float>* result_distances);
    
private:
    // S2 Cell Index: cell_id → partition
    struct CellIndexEntry {
        S2CellId cell_id;
        uint64_t offset;
        uint32_t count;
    };
    std::vector<CellIndexEntry> _cell_index;
    
    // Resolve spatial query to matching partitions
    std::vector<size_t> resolve_partitions(
        const S2Region& query_region) const;
    
    // Search a single partition's HNSW
    Status search_partition(
        size_t partition_idx,
        const float* query_vector,
        int k,
        int ef_search,
        std::vector<uint32_t>* row_ids,
        std::vector<float>* distances);
    
    // Merge results across partitions
    void merge_results(
        int k,
        const std::vector<std::vector<uint32_t>>& partition_row_ids,
        const std::vector<std::vector<float>>& partition_distances,
        std::vector<uint32_t>* merged_row_ids,
        std::vector<float>* merged_distances);
};
```

### HybridSearchOption

```cpp
struct HybridSearchOption {
    // Vector search parameters (from existing VectorSearchOption)
    int k;
    const float* query_vector;
    uint32_t dim;
    int ef_search;
    std::string distance_column;
    
    // Spatial parameters (new)
    std::unique_ptr<S2Region> spatial_region;  // Query region
    int search_strategy;  // From TSpatialSearchOptions
    double selectivity_estimate;
    
    // Optional: row ID filter from other predicates
    const roaring::Roaring* row_id_filter = nullptr;
};
```

## Segment Iterator Integration

In `be/src/storage/rowset/segment_iterator.cpp`:

```cpp
// In _init_hybrid_vector_index():
if (_opts.use_hybrid_vector_index && _segment->has_hybrid_vector_index()) {
    _hybrid_vector_reader = std::make_unique<HybridVectorIndexReader>();
    RETURN_IF_ERROR(_hybrid_vector_reader->open(
        _segment->hybrid_vector_index_path()));
}

// In _do_hybrid_vector_search():
if (_hybrid_vector_reader) {
    HybridSearchOption option;
    option.k = _opts.vector_search_option->k;
    option.query_vector = _opts.vector_search_option->query_vector;
    option.dim = _opts.vector_search_option->dim;
    option.ef_search = _opts.vector_search_option->ef_search;
    option.spatial_region = _opts.hybrid_search_option->spatial_region;
    option.search_strategy = _opts.hybrid_search_option->search_strategy;
    
    std::vector<uint32_t> row_ids;
    std::vector<float> distances;
    RETURN_IF_ERROR(_hybrid_vector_reader->search(
        option, &row_ids, &distances));
    
    // Set result range and distances
    _hybrid_vector_result_range.add_ranges(row_ids);
    _hybrid_vector_distances = std::move(distances);
}
```

## Data Flow Summary

```
SegmentWriter::append_chunk()
  → ColumnWriter::append(location_column)  → stores lat/lon
  → ColumnWriter::append(embedding_column) → stores vectors
  → HybridVectorIndexBuilder::add(row_id, embedding, lat, lon)
     → Computes S2 cell ID
     → Buffers into partition

SegmentWriter::finalize()
  → HybridVectorIndexBuilder::finish()
     → For each partition: build HNSW via TenANN
     → Write .hvi file with all partitions

SegmentIterator::init()
  → HybridVectorIndexReader::open(.hvi file)
     → Read header, S2 Cell Index
  
SegmentIterator::_do_hybrid_vector_search()
  → HybridVectorIndexReader::search(option)
     → resolve_partitions(spatial_region) → matching partition indices
     → For each matching partition: search_partition(query_vector, k)
     → merge_results() → global top-k
  → Return row IDs + distances
```

## Compaction Handling

During compaction (segment merge), the hybrid index is rebuilt:

```cpp
// In RowsetWriter or CompactionTask:
// 1. Read source segments' data (location + embedding columns)
// 2. Create new HybridVectorIndexBuilder for merged segment
// 3. Feed all rows from source segments
// 4. finish() builds new partitioned HNSW
// 5. Delete old .hvi files
```

This follows the same pattern as existing vector index compaction.
