# Test Queries

## Query Templates

### Template 1: Point-in-Polygon + K-ANN

```sql
SELECT id, name,
       approx_l2_distance(embedding, ?) AS dist
FROM geo_vectors
WHERE ST_Contains(
    ST_GeomFromText(?),
    location
)
ORDER BY dist
LIMIT ?;
```

Parameters: query_vector, polygon_wkt, k

### Template 2: Radius + K-ANN

```sql
SELECT id, name,
       approx_l2_distance(embedding, ?) AS dist
FROM geo_vectors
WHERE ST_Distance_Sphere(
    ST_X(location), ST_Y(location), ?, ?
) < ?
ORDER BY dist
LIMIT ?;
```

Parameters: query_vector, center_lon, center_lat, radius_meters, k

### Template 3: Combined Spatial + Scalar + K-ANN

```sql
SELECT id, name, category,
       approx_l2_distance(embedding, ?) AS dist
FROM geo_vectors
WHERE ST_Contains(ST_GeomFromText(?), location)
  AND category IN (?, ?, ?)
ORDER BY dist
LIMIT ?;
```

Parameters: query_vector, polygon_wkt, category_values, k

### Template 4: Cosine Similarity Variant

```sql
SELECT id, name,
       approx_cosine_similarity(embedding, ?) AS sim
FROM geo_vectors
WHERE ST_Contains(ST_GeomFromText(?), location)
ORDER BY sim DESC
LIMIT ?;
```

Parameters: query_vector, polygon_wkt, k

## Concrete Test Queries (California OSM Dataset)

### Q1: Downtown San Francisco — Small Region

```sql
-- σ_s ≈ 0.5% (small area, many POIs)
SELECT id, name, approx_l2_distance(embedding, @query_vec) AS dist
FROM california_pois
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((-122.42 37.79, -122.39 37.79,
                              -122.39 37.77, -122.42 37.77,
                              -122.42 37.79))'),
    location)
ORDER BY dist LIMIT 10;
```

### Q2: Greater Los Angeles — Large Region

```sql
-- σ_s ≈ 15% (large metro area)
SELECT id, name, approx_l2_distance(embedding, @query_vec) AS dist
FROM california_pois
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((-118.7 34.2, -117.7 34.2,
                              -117.7 33.7, -118.7 33.7,
                              -118.7 34.2))'),
    location)
ORDER BY dist LIMIT 10;
```

### Q3: Single Block — Micro Region

```sql
-- σ_s ≈ 0.01% (single city block)
SELECT id, name, approx_l2_distance(embedding, @query_vec) AS dist
FROM california_pois
WHERE ST_Distance_Sphere(
    ST_X(location), ST_Y(location),
    -122.4194, 37.7749
) < 200  -- 200 meters
ORDER BY dist LIMIT 10;
```

### Q4: Northern California — Medium Region

```sql
-- σ_s ≈ 30%
SELECT id, name, approx_l2_distance(embedding, @query_vec) AS dist
FROM california_pois
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((-123.0 38.5, -121.0 38.5,
                              -121.0 37.0, -123.0 37.0,
                              -123.0 38.5))'),
    location)
ORDER BY dist LIMIT 10;
```

### Q5: Entire California — Near-Full Region

```sql
-- σ_s ≈ 95%
SELECT id, name, approx_l2_distance(embedding, @query_vec) AS dist
FROM california_pois
WHERE ST_Contains(california_boundary_polygon, location)
ORDER BY dist LIMIT 10;
```

## Query Vector Generation

Query vectors should be:

1. **From dataset**: Use actual embeddings from the dataset as query vectors (most realistic)
2. **Random**: Uniformly random vectors (worst case for ANN indexes)
3. **Perturbed**: Take a dataset vector, add small noise (tests near-duplicate retrieval)
4. **Out-of-distribution**: Vectors from a different distribution (tests robustness)

## Query Set Sizes

| Test Type | Queries | Purpose |
|-----------|---------|---------|
| Correctness | 100 | Verify recall against ground truth |
| Latency | 1,000 | Measure latency percentiles |
| Throughput | 10,000 | Measure sustained QPS |
| Stress | 100,000 | Find performance degradation |

## Selectivity Sweep

Generate query sets at each selectivity level:

```python
selectivity_levels = [0.001, 0.005, 0.01, 0.05, 0.10, 0.25, 0.50, 0.90]

for sigma in selectivity_levels:
    # Generate spatial region that covers σ fraction of data
    region = generate_region_with_selectivity(dataset, sigma)
    # Generate 1000 queries with this region
    queries = [(random_query_vector(), region) for _ in range(1000)]
    save_query_set(f"queries_sigma_{sigma}.json", queries)
```

This is the most important test set — it reveals how each approach performs across the selectivity spectrum, which is the key differentiator for strategy selection.
