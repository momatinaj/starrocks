# S2 Integration

## Existing S2 Usage in StarRocks

StarRocks already uses Google S2 Geometry library for spatial functions:

### Current Integration Points

| Component | File | Usage |
|-----------|------|-------|
| GeoPoint | `be/src/geo/geo_types.h` | `S2Point` wrapper |
| GeoLine | `be/src/geo/geo_types.h` | `S2Polyline` wrapper |
| GeoPolygon | `be/src/geo/geo_types.h` | `S2Polygon` wrapper |
| GeoCircle | `be/src/geo/geo_types.h` | `S2Cap` wrapper |
| ST_Contains | `be/src/exprs/geo_functions.cpp` | `S2Polygon::Contains(S2Point)` |
| ST_Distance_Sphere | `be/src/exprs/geo_functions.cpp` | `S2Earth::ToMeters(S2Point, S2Point)` |

### S2 Headers Available

StarRocks has access to the full S2 library via thirdparty:
- `s2/s2cell_id.h` — Hierarchical cell IDs
- `s2/s2cell.h` — Cell geometry
- `s2/s2region_coverer.h` — Region → cell covering
- `s2/s2polygon.h` — Polygon operations
- `s2/s2cap.h` — Spherical cap (circle)
- `s2/s2earth.h` — Earth-specific utilities

## New S2 Usage for Hybrid Index

### S2CellId for Partitioning

```cpp
#include <s2/s2cell_id.h>
#include <s2/s2latlng.h>

S2CellId compute_cell_id(double lat, double lon, int level) {
    S2LatLng latlng = S2LatLng::FromDegrees(lat, lon);
    S2CellId cell_id(latlng.ToPoint());
    return cell_id.parent(level);  // Truncate to desired level
}
```

### Region Covering for Query Resolution

```cpp
#include <s2/s2region_coverer.h>
#include <s2/s2polygon.h>

std::vector<S2CellId> compute_covering(
        const S2Region& region, int level) {
    S2RegionCoverer::Options options;
    options.set_fixed_level(level);
    S2RegionCoverer coverer(options);
    
    std::vector<S2CellId> covering;
    coverer.GetCovering(region, &covering);
    return covering;
}
```

### S2 Cell Containment Check (Fast Path)

```cpp
enum class CellContainment {
    OUTSIDE,      // Cell entirely outside region
    INSIDE,       // Cell entirely inside region (all points pass)
    PARTIAL       // Cell partially overlaps (need per-point check)
};

CellContainment check_cell_containment(
        const S2Region& region, S2CellId cell_id) {
    S2Cell cell(cell_id);
    if (!region.MayIntersect(cell)) {
        return CellContainment::OUTSIDE;
    }
    if (region.Contains(cell)) {
        return CellContainment::INSIDE;
    }
    return CellContainment::PARTIAL;
}
```

This three-way check is the key optimization for filtered HNSW traversal — nodes in "INSIDE" cells skip per-point containment checks entirely.

### Converting Existing Geometry to S2Region

StarRocks stores geometry as binary in VARCHAR. We need to convert to S2Region for covering computation:

```cpp
#include "geo/geo_types.h"

std::unique_ptr<S2Region> geometry_to_s2region(
        const GeoShape* shape) {
    switch (shape->type()) {
        case GEO_SHAPE_POLYGON:
            return static_cast<const GeoPolygon*>(shape)->polygon()->Clone();
        case GEO_SHAPE_CIRCLE: {
            auto circle = static_cast<const GeoCircle*>(shape);
            // Convert to S2Cap
            return std::make_unique<S2Cap>(circle->cap());
        }
        default:
            return nullptr;  // Unsupported for indexing
    }
}
```

## S2 Cell Statistics

For selectivity estimation, maintain per-cell vector counts:

```cpp
struct S2CellStatistics {
    std::unordered_map<S2CellId, uint32_t> cell_counts;
    uint32_t total_count;
    
    double estimate_selectivity(
            const std::vector<S2CellId>& covering) const {
        uint32_t matching = 0;
        for (const auto& cell : covering) {
            auto it = cell_counts.find(cell);
            if (it != cell_counts.end()) {
                matching += it->second;
            }
        }
        return static_cast<double>(matching) / total_count;
    }
};
```

## Performance Considerations

### S2 Operation Costs

| Operation | Typical Cost | Notes |
|-----------|-------------|-------|
| `S2CellId` from lat/lon | ~50 ns | Fast, integer arithmetic |
| `S2CellId::parent(level)` | ~5 ns | Bit shift |
| `S2RegionCoverer::GetCovering` | ~1-10 μs | Depends on region complexity |
| `S2Region::MayIntersect(cell)` | ~20-100 ns | Bounding check |
| `S2Region::Contains(cell)` | ~50-200 ns | Full containment |
| `S2Polygon::Contains(point)` | ~100-500 ns | Point-in-polygon |

### Memory for S2 Cell IDs

- 8 bytes per cell ID (uint64)
- For 10K vectors per segment: 80 KB
- For 100K vectors per segment: 800 KB
- Negligible compared to vector data (10K × 512D × 4B = 20 MB)

## Limitations

### S2 Cell Boundary Effects

Vectors near S2 cell boundaries may be close in geographic space but assigned to different partitions. Mitigations:
1. **Overlap margin**: Assign vectors within distance D of cell boundary to both cells
2. **Cross-partition edges**: Stitch HNSW graphs at cell boundaries
3. **Higher-level covering**: Use coarser cells at query time to include neighbors

### Fixed Grid

S2 cells are a fixed grid — they don't adapt to data density. Dense urban areas may have many vectors per cell while rural areas have few. Mitigations:
1. **Adaptive level**: Use finer S2 level in dense cells, coarser in sparse cells
2. **Merge small cells**: Combine adjacent cells with few vectors into parent cell
3. **Split large cells**: Break cells exceeding max partition size into children

### Spherical Geometry

S2 operates on the sphere, not a plane. For small regions this doesn't matter, but for large regions:
- Cell shapes are not rectangular (they're quadrilaterals on the sphere)
- Distance calculations are geodesic, not Euclidean
- This is actually an advantage over geohash (which distorts at high latitudes)
