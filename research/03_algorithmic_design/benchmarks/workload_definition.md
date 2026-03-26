# Workload Definition

## Query Patterns

### Pattern 1: City-Scale Visual Search
"Find the 10 most similar images to this photo within downtown San Francisco"

```sql
SELECT id, image_url,
       approx_l2_distance(embedding, ?) AS dist
FROM street_images
WHERE ST_Contains(
    ST_GeomFromText('POLYGON((-122.42 37.78, -122.40 37.78,
                              -122.40 37.76, -122.42 37.76,
                              -122.42 37.78))'),
    location
)
ORDER BY dist
LIMIT 10;
```

- Spatial selectivity: ~1-5% (downtown area of a city)
- Vector dimension: 512-2048 (image embeddings)
- Expected result set: 10 (top-k)

### Pattern 2: Neighborhood-Scale POI Search
"Find 5 restaurants most similar to my favorite restaurant within 2km"

```sql
SELECT name, cuisine,
       approx_cosine_similarity(embedding, ?) AS sim
FROM restaurants
WHERE ST_DWithin(location, ST_Point(-122.4194, 37.7749), 2000)
ORDER BY sim DESC
LIMIT 5;
```

- Spatial selectivity: ~0.1-1% (2km radius in a city)
- Vector dimension: 128-512 (review/menu embeddings)

### Pattern 3: Country-Scale Geospatial Analytics
"Find the 100 most anomalous sensor readings in California this month"

```sql
SELECT sensor_id, reading_time,
       approx_l2_distance(feature_vector, ?) AS anomaly_score
FROM sensor_data
WHERE ST_Contains(california_polygon, sensor_location)
  AND reading_time >= '2025-03-01'
ORDER BY anomaly_score
LIMIT 100;
```

- Spatial selectivity: ~10-30% (state-level region)
- Temporal filter: further reduces candidates
- Vector dimension: 64-256 (sensor feature vectors)

### Pattern 4: Micro-Region High-Density Search
"Find 20 most similar faces detected at this intersection"

```sql
SELECT detection_id, timestamp,
       approx_l2_distance(face_embedding, ?) AS dist
FROM face_detections
WHERE ST_DWithin(camera_location, ST_Point(-73.9857, 40.7484), 100)
ORDER BY dist
LIMIT 20;
```

- Spatial selectivity: ~0.01-0.1% (100m radius)
- Vector dimension: 128-512 (face embeddings)
- High density: many vectors in small area

## Spatial–Vector Correlation Scenarios (ρ)

Benchmarks should **fix** a query pattern and **σ_s**, then **sweep** synthetic or labeled datasets across **ρ** (see [spatial_vector_correlation.md](../spatial_vector_correlation.md) and [datasets.md](../../05_evaluation/datasets.md)).

| Scenario | ρ (informal) | What to stress-test |
|----------|----------------|---------------------|
| **Aligned** | Positive | Spatial-partitioned HNSW vs. cascade; vector-first **oversampling** should **shrink** vs. independence baseline. |
| **Independent** | ≈ 0 | Filtered ANN (ACORN); vector-first **oversampling ≈ 1/σ_s**; partitions help **only** as pruning. |
| **Diverse locality** | Negative | Partition **boundary recall**; **stitching** on/off; **efSearch** / 2-hop tuning; **failure** of naive vector-first. |

**Minimum matrix:** (selectivity bucket) × (ρ bin) × (strategy: cascade / filtered HNSW / partitioned / vector-first).

## Selectivity Distribution

| Pattern | Selectivity Range | Primary Challenge |
|---------|------------------|-------------------|
| City-scale | 1-5% | Moderate filtering, good for ACORN |
| Neighborhood | 0.1-1% | Selective, partition-based excels |
| Country-scale | 10-30% | Large result set, post-filter viable |
| Micro-region | 0.01-0.1% | Very selective, brute-force competitive |

## Data Scale Parameters

| Parameter | Small | Medium | Large | XLarge |
|-----------|-------|--------|-------|--------|
| Total vectors | 100K | 1M | 10M | 100M |
| Vector dimension | 128 | 512 | 768 | 2048 |
| Segments | 10 | 100 | 1000 | 10000 |
| Vectors/segment | 10K | 10K | 10K | 10K |
| Geographic extent | City | State | Country | Global |
| Data density | Uniform | Urban-clustered | Mixed | Highly skewed |

## Performance Metrics

See [../metrics.md](../../05_evaluation/metrics.md) for detailed metric definitions.

| Metric | Target (City-scale) | Target (Neighborhood) |
|--------|--------------------|-----------------------|
| Recall@10 | ≥ 0.95 | ≥ 0.90 |
| p50 Latency | < 10ms | < 5ms |
| p99 Latency | < 50ms | < 20ms |
| QPS (single node) | > 1000 | > 2000 |
| Index build time (10K vectors) | < 1s | < 1s |
| Memory overhead | < 2x vector data | < 2x vector data |
