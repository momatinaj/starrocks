# Index Type Design

## New Index Type

### Proto Definition

In `gensrc/proto/types.proto`:
```protobuf
enum IndexType {
    BITMAP = 0;
    GIN = 1;
    NGRAMBF = 3;
    VECTOR = 4;
    SPATIAL_VECTOR = 5;  // NEW: Hybrid spatial+vector index
}
```

### Thrift Definition

In `gensrc/thrift/Descriptors.thrift`:
```thrift
enum TIndexType {
    BITMAP = 0,
    GIN = 1,
    NGRAMBF = 3,
    VECTOR = 4,
    SPATIAL_VECTOR = 5  // NEW
}
```

## Index Properties

The hybrid index requires properties for both spatial and vector components:

### Spatial Properties

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `s2_level` | int | 12 | S2 cell level for partitioning (10-16) |
| `s2_level_min` | int | 10 | Min level for adaptive partitioning |
| `s2_level_max` | int | 16 | Max level for adaptive partitioning |
| `spatial_column` | string | required | Column name containing location data |
| `min_partition_size` | int | 50 | Min vectors per partition to build HNSW |
| `enable_cross_partition_edges` | bool | false | Stitch adjacent partition HNSW graphs |

### Vector Properties (inherited from VECTOR index)

| Property | Type | Default | Description |
|----------|------|---------|-------------|
| `index_type` | string | "HNSW" | ANN algorithm (HNSW, IVFPQ) |
| `dim` | int | required | Vector dimension |
| `metric_type` | string | "L2" | Distance metric (L2, COSINE, IP) |
| `M` | int | 16 | HNSW max connections |
| `efconstruction` | int | 200 | HNSW build-time beam width |
| `efsearch` | int | 200 | HNSW search-time beam width |

## File Format: .hvi (Hybrid Vector Index)

```
File: {rowset}_{segment}_{index_id}.hvi

┌─────────────────────────────────────────┐
│ Header                                   │
│ ├── Magic number (4 bytes): "SRHI"       │
│ ├── Version (4 bytes)                    │
│ ├── S2 level (4 bytes)                   │
│ ├── Metric type (4 bytes)                │
│ ├── Vector dimension (4 bytes)           │
│ ├── Total vectors (8 bytes)              │
│ ├── Partition count (4 bytes)            │
│ └── Header checksum (4 bytes)            │
├─────────────────────────────────────────┤
│ S2 Cell Index                            │
│ ├── Cell ID array (uint64[partition_ct]) │
│ ├── Offset array (uint64[partition_ct])  │
│ ├── Count array (uint32[partition_ct])   │
│ └── Cell statistics (optional)           │
├─────────────────────────────────────────┤
│ Partition 0                              │
│ ├── HNSW graph data (TenANN format)      │
│ ├── Vector data (float[count × dim])     │
│ ├── Row ID mapping (uint32[count])       │
│ └── Spatial coords (float[count × 2])    │
├─────────────────────────────────────────┤
│ Partition 1                              │
│ ├── ...                                  │
├─────────────────────────────────────────┤
│ ...                                      │
├─────────────────────────────────────────┤
│ Cross-Partition Edges (optional)         │
│ ├── Edge count (4 bytes)                 │
│ └── Edges (src_partition, src_node,      │
│          dst_partition, dst_node)[]       │
├─────────────────────────────────────────┤
│ Footer                                   │
│ ├── S2 Cell Index offset (8 bytes)       │
│ ├── Partition offsets table (8 bytes)     │
│ └── File checksum (4 bytes)              │
└─────────────────────────────────────────┘
```

## Index Descriptor Extension

In `be/src/storage/index/index_descriptor.h`:

```cpp
std::string hybrid_vector_index_file_path() const {
    return fmt::format("{}_{}_{}.hvi",
        _rowset_id, _segment_id, _index_id);
}
```

Add to `get_index_file_path()` switch:
```cpp
case IndexType::SPATIAL_VECTOR:
    return hybrid_vector_index_file_path();
```

## FE Index Definition

In `fe/fe-core/src/main/java/com/starrocks/catalog/IndexDef.java`:

```java
public enum IndexType {
    BITMAP,
    GIN,
    NGRAMBF,
    VECTOR,
    SPATIAL_VECTOR  // NEW
}
```

## TabletIndex Extension

In `be/src/storage/tablet_index.h`, no changes needed — the existing `TabletIndex` class stores type, column UIDs, and properties generically. The hybrid index uses:
- `type()` → `SPATIAL_VECTOR`
- `col_unique_id()` → embedding column UID
- `properties()` → map containing both spatial and vector properties
- Additional column UID for spatial column stored in properties as `spatial_column_uid`

## Build Threshold

Like the existing vector index, the hybrid index should not be built for very small segments:

```cpp
// In config_vector_index_fwd.h or new config file
CONF_Int32(hybrid_vector_index_default_build_threshold, "100");
```

If a segment has fewer than `threshold` rows, skip hybrid index construction and fall back to brute-force search via `EmptyIndexReader`.
