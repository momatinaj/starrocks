# TenANN Extensions

## Current TenANN Usage

TenANN (v0.5.0) is StarRocks' ANN library providing HNSW and IVFPQ index types:

| Component | File | Purpose |
|-----------|------|---------|
| TenANN Builder | `be/src/storage/index/vector/tenann/tenann_index_builder.cpp` | Builds HNSW/IVFPQ from vectors |
| TenANN Reader | `be/src/storage/index/vector/tenann_index_reader.cpp` | Searches built index |
| TenANN Utils | `be/src/storage/index/vector/tenann/tenann_index_utils.cpp` | Meta conversion |
| Del ID Filter | `be/src/storage/index/vector/tenann/del_id_filter.h` | Row ID filtering during search |
| Config | `be/src/common/config_vector_index_fwd.h` | Build/search parameters |

## Extension Options

### Option A: Extend TenANN for Filtered Search (Tier 1)

Modify TenANN's HNSW search to support predicate-aware traversal:

```cpp
// New: Predicate-aware search interface
class PredicateFilter {
public:
    virtual ~PredicateFilter() = default;
    virtual bool passes(uint32_t node_id) const = 0;
    virtual FilterResult check_batch(
        const uint32_t* node_ids, 
        size_t count,
        bool* results) const = 0;
};

class S2CellPredicateFilter : public PredicateFilter {
public:
    S2CellPredicateFilter(
        const S2Region& region,
        const S2CellId* node_cell_ids,
        const float* node_lats,
        const float* node_lons);
    
    bool passes(uint32_t node_id) const override {
        // Three-level check: cell outside → cell inside → point check
        S2CellId cell = _node_cell_ids[node_id];
        if (!_region.MayIntersect(S2Cell(cell))) return false;
        if (_region.Contains(S2Cell(cell))) return true;
        S2Point point = S2LatLng::FromDegrees(
            _node_lats[node_id], _node_lons[node_id]).ToPoint();
        return _region.Contains(point);
    }
};
```

**TenANN HNSW search modification** (ACORN-style):

```cpp
// Extend the HNSW search loop:
// When a neighbor fails the predicate:
//   If selectivity < blind_threshold:
//     Explore all of neighbor's neighbors (2-hop blind)
//   If selectivity < directed_threshold:
//     Explore neighbor's neighbors closer to query (2-hop directed)
//   Else:
//     Skip (standard 1-hop)
```

**Effort**: Moderate. Requires modifying TenANN's HNSW search implementation.
**Risk**: Medium. TenANN is a third-party library; modifications may conflict with future versions.

### Option B: Multiple TenANN Instances per Partition (Tier 2)

Use TenANN as-is, but create one instance per S2 cell partition:

```cpp
class SpatialPartitionedHNSW {
    struct Partition {
        S2CellId cell_id;
        std::unique_ptr<TenANNIndexBuilder> builder;
        std::unique_ptr<TenANNIndexReader> reader;
        std::string index_path;
    };
    
    std::vector<Partition> _partitions;
    
    Status build(const std::vector<PartitionData>& data) {
        for (auto& partition : data) {
            auto builder = TenANNIndexBuilderFactory::create(...);
            builder->build(partition.embeddings);
            _partitions.push_back({partition.cell_id, builder, ...});
        }
    }
    
    Status search(const S2Region& region, 
                  const float* query_vector, int k,
                  std::vector<Result>* results) {
        auto matching = resolve_partitions(region);
        for (auto& partition : matching) {
            auto reader = partition.reader;
            auto partition_results = reader->search(query_vector, k);
            results->merge(partition_results);
        }
        // Return global top-k
    }
};
```

**Effort**: Low. Uses TenANN as-is.
**Risk**: Low. No TenANN modifications.
**Limitation**: No cross-partition graph traversal (but this is acceptable for initial implementation).

### Option C: Replace TenANN with Custom Implementation (Future)

If TenANN proves too limiting, implement a custom HNSW with native spatial support:

```cpp
class SpatialHNSW {
    struct Node {
        uint32_t id;
        float* vector;         // D-dimensional embedding
        S2CellId cell_id;      // Spatial cell
        float lat, lon;        // Exact coordinates
        std::vector<uint32_t> connections;  // Per-layer neighbors
    };
    
    // Custom search with built-in spatial awareness
    std::vector<Result> search(
        const float* query_vector,
        const S2Region* spatial_region,
        int k, int ef_search);
};
```

**Effort**: Very High. Complete HNSW implementation.
**Risk**: High. Must match TenANN's performance for non-spatial queries.
**Benefit**: Full control over graph construction and traversal.

## Recommendation

**Phase 1 (Tier 0-1)**: Option B — multiple TenANN instances per partition.
- Simplest to implement
- Uses TenANN as-is (no modification risk)
- Can be enhanced later with Option A for filtered traversal within partitions

**Phase 2 (Tier 1-2)**: Option A — extend TenANN for ACORN-style traversal.
- Provides the best performance for moderate-selectivity queries
- Requires TenANN modification but is architecturally sound
- Can be combined with Option B (filtered search within partitions)

**Future**: Option C — only if TenANN is deprecated or fundamentally incompatible.
- Provides maximum control but requires significant engineering investment
- Justified only for research/publication purposes or if TenANN is abandoned

## TenANN Dependency Management

TenANN is built from source as a thirdparty dependency:
- Source: `StarRocks/tenann` (v0.5.0)
- Build flags: `WITH_TENANN` (default ON for Linux x86_64/arm64)
- CMake: `be/cmake_modules/ThirdParty.cmake`

For Option A (TenANN modification):
- Fork TenANN or contribute upstream
- Maintain patch set if forking
- Consider contributing ACORN-style filtered search back to TenANN

For Option B (multiple instances):
- No TenANN changes needed
- Ensure TenANN supports multiple concurrent instances (likely yes, each is independent)
- Memory overhead: multiple HNSW graphs have per-graph overhead
