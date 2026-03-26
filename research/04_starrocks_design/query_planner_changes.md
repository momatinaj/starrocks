# Query Planner Changes

## Overview

The FE query planner must detect queries combining spatial predicates with vector similarity search and generate an optimized execution plan. This extends the existing `RewriteToVectorPlanRule`.

## Pattern Detection

### Target SQL Patterns

```sql
-- Pattern 1: ST_Contains + approx_l2_distance
SELECT *, approx_l2_distance(embedding, [0.1, ...]) AS dist
FROM table
WHERE ST_Contains(polygon, location)
ORDER BY dist
LIMIT 10;

-- Pattern 2: ST_DWithin + approx_cosine_similarity
SELECT *, approx_cosine_similarity(embedding, [0.1, ...]) AS sim
FROM table
WHERE ST_DWithin(location, ST_Point(-122.4, 37.7), 5000)
ORDER BY sim DESC
LIMIT 10;

-- Pattern 3: Combined with other predicates
SELECT *, approx_l2_distance(embedding, [0.1, ...]) AS dist
FROM table
WHERE ST_Contains(polygon, location)
  AND category = 'sign'
  AND created_at > '2025-01-01'
ORDER BY dist
LIMIT 10;
```

### Logical Plan Matching

The existing `RewriteToVectorPlanRule` matches:
```
LogicalTopN
└── LogicalProject (with approx_l2_distance / approx_cosine_similarity)
    └── LogicalOlapScan (with predicates)
```

Extend to also recognize spatial predicates in the predicate tree:
- `ST_Contains(geometry, column)` → spatial filter
- `ST_Within(column, geometry)` → spatial filter (reversed args)
- `ST_DWithin(column, point, distance)` → spatial filter
- `ST_Intersects(geometry, column)` → spatial filter

## New/Modified Optimizer Rules

### Option A: Extend RewriteToVectorPlanRule (Recommended for Tier 0-1)

```java
// In RewriteToVectorPlanRule.java

// Existing: extracts vector search options from TopN+Scan
// New: also extract spatial predicate if present

private Optional<SpatialSearchOptions> extractSpatialPredicate(
        PredicateOperator predicate) {
    // Check if predicate is a spatial function call
    if (isSpatialFunction(predicate)) {
        SpatialSearchOptions options = new SpatialSearchOptions();
        options.setSpatialFunction(predicate.getFunctionName());
        options.setGeometryArg(extractGeometry(predicate));
        options.setSpatialColumn(extractSpatialColumn(predicate));
        return Optional.of(options);
    }
    return Optional.empty();
}
```

### Option B: New RewriteToHybridVectorPlanRule (Recommended for Tier 2)

A dedicated rule that generates a `PhysicalHybridVectorScan` operator:

```java
public class RewriteToHybridVectorPlanRule extends TransformationRule {
    @Override
    public List<OptExpression> transform(OptExpression input) {
        // 1. Match pattern: TopN over Project over OlapScan
        // 2. Extract vector search options (existing logic)
        // 3. Extract spatial predicate (new logic)
        // 4. Check if table has SPATIAL_VECTOR index on both columns
        // 5. Estimate spatial selectivity
        // 6. Choose strategy based on selectivity
        // 7. Generate HybridVectorSearchOptions
        // 8. Return rewritten plan with hybrid search
    }
}
```

## Spatial–Vector Correlation Hint (Optional, Future)

The cost model in [cost_model.md](../../03_algorithmic_design/cost_model.md) can combine **σ̂_s** with **ρ̂** (estimated **spatial–vector correlation**). The planner may:

- Read **per-table / per-segment** statistics computed at **compaction** (sampling pairs: correlation between geo distance and vector distance ranks).
- Accept a **table property** or **session variable** `correlation_hint = {positive|neutral|negative}` for expert tuning when statistics are absent.

See [spatial_vector_correlation.md](../../03_algorithmic_design/spatial_vector_correlation.md).

## Selectivity Estimation in Planner

```java
// In a new class: SpatialSelectivityEstimator.java

public class SpatialSelectivityEstimator {
    
    public double estimateSelectivity(
            OlapTable table,
            SpatialSearchOptions spatialOptions) {
        
        // Option 1: Use column statistics (min/max lat/lon)
        ColumnStatistic locStats = table.getColumnStatistic(
            spatialOptions.getSpatialColumn());
        
        // Estimate fraction of bounding box covered by spatial region
        double tableBboxArea = computeBboxArea(locStats);
        double queryArea = computeQueryArea(spatialOptions);
        double selectivity = queryArea / tableBboxArea;
        
        // Clamp to [0, 1]
        return Math.max(0.0, Math.min(1.0, selectivity));
    }
}
```

## Thrift Extensions

### VectorSearchOptions Extension

In `gensrc/thrift/PlanNodes.thrift`:

```thrift
struct TSpatialSearchOptions {
    1: optional string spatial_function    // "ST_Contains", "ST_DWithin", etc.
    2: optional binary geometry_wkb        // Serialized geometry
    3: optional i32 spatial_column_slot_id
    4: optional double selectivity_estimate
    5: optional i32 search_strategy        // 0=brute_force, 1=filtered_hnsw, 2=partitioned
}

// Extend existing TVectorSearchOptions:
struct TVectorSearchOptions {
    // ... existing fields ...
    20: optional TSpatialSearchOptions spatial_options  // NEW
}
```

## Strategy Selection in Planner

```java
enum HybridSearchStrategy {
    BRUTE_FORCE_ON_FILTERED,    // σ_s < 0.001
    ACORN_BLIND_TWO_HOP,        // 0.001 ≤ σ_s < 0.08
    ACORN_DIRECTED_TWO_HOP,     // 0.08 ≤ σ_s < 0.40
    STANDARD_HNSW_POST_FILTER,  // 0.40 ≤ σ_s < 0.90
    STANDARD_HNSW_IGNORE,       // σ_s ≥ 0.90
    SPATIAL_PARTITIONED          // When SPATIAL_VECTOR index exists
}
```

The planner evaluates selectivity and encodes the chosen strategy in `TSpatialSearchOptions.search_strategy`, so the BE executor doesn't need to re-estimate.

## Testing

New test class: `HybridVectorIndexTest.java` covering:
- Spatial+vector query pattern recognition
- Selectivity estimation accuracy
- Strategy selection for various selectivity ranges
- Plan generation with hybrid search options
- Edge cases: no spatial index, no vector index, overlapping predicates
