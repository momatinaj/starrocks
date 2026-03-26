# Datasets

## Requirements

Datasets must have:
1. **Geographic coordinates** (latitude, longitude) for each record
2. **High-dimensional vector embeddings** for each record
3. **Sufficient scale** to stress-test the index (100K-10M records)
4. **Realistic spatial distribution** (urban clusters, rural sparse areas)

## Real-World Datasets

### OpenStreetMap POIs + CLIP Embeddings

**Source**: OpenStreetMap exports + generated embeddings  
**Description**: Points of interest (restaurants, shops, landmarks) with locations and visual/text embeddings generated from OSM tags and nearby Street View imagery.

**Generation pipeline**:
1. Download OSM PBF file for target region (e.g., California, Germany, Japan)
2. Extract POIs with `osmium` or `pyosmium`
3. Generate text embeddings from POI tags (name, amenity, cuisine, etc.) using CLIP or sentence-transformers
4. Output: (id, lat, lon, embedding[512], category, name)

**Scale**: ~1M POIs for California, ~5M for Germany, ~50M globally  
**Distribution**: Highly clustered (urban areas dense, rural areas sparse)

### GeoLife Trajectories + Motion Embeddings

**Source**: Microsoft GeoLife dataset  
**Description**: GPS trajectories from 182 users in Beijing over 5 years.

**Adaptation**: Convert trajectory segments to motion feature vectors (speed, direction, duration, acceleration patterns).

**Scale**: ~17K trajectories, ~24M GPS points  
**Distribution**: Concentrated in Beijing metropolitan area

### YFCC100M (Yahoo Flickr Creative Commons)

**Source**: Yahoo Flickr Creative Commons 100M dataset  
**Description**: 100M Flickr photos with geotags and metadata.

**Adaptation**: 
1. Use pre-computed CLIP embeddings (available from various sources)
2. Filter to geotagged photos (~48M have coordinates)
3. Output: (id, lat, lon, embedding[512])

**Scale**: ~48M geotagged photos  
**Distribution**: Global, heavily biased toward tourist locations

### ANN Benchmarks Standard Datasets + Synthetic Geo

**Source**: ann-benchmarks.com standard datasets (SIFT, GIST, GloVe) + synthetic coordinates

**Adaptation**: 
1. Take standard ANN benchmark dataset (e.g., SIFT1M)
2. Assign realistic geographic coordinates using spatial distribution models:
   - Uniform random (baseline)
   - Gaussian mixture (clustered cities)
   - Power-law density (realistic urban/rural)
3. This isolates the spatial+vector interaction from dataset-specific effects

**Scale**: 1M (SIFT1M), 1B (SIFT1B)

## Synthetic Datasets

### Controlled Spatial+Vector Correlation (ρ)

Generate data where the **joint behavior** of space and vectors matches **positive**, **near-zero**, or **negative** correlation scenarios. This is **required** to validate [spatial_vector_correlation.md](../03_algorithmic_design/spatial_vector_correlation.md) and **correlation-aware** tuning in [cost_model.md](../03_algorithmic_design/cost_model.md).

**Interpretation:**

| Target regime | Generation idea |
|---------------|-----------------|
| **Positive ρ** | `vector_i ≈ f(location_i) + noise` with small noise (e.g., `location_to_vector` from lat/lon, or cluster centers in both spaces). |
| **ρ ≈ 0** | Draw **locations** and **vectors** **independently** (optionally same marginal distributions as real data). |
| **Negative ρ** | **Anti-align**: similar **locations** → **dissimilar** vectors — e.g., assign **per-cell** or **per-block** **orthogonal** prototype vectors; or `vector = g(location) + h(block_id)` where **within-block** diversity is **forced**. |

**Sketch (scalar control parameter `correlation` ∈ [−1, 1] or two knobs):**

```python
def generate_dataset(n, dim, correlation):
    """
    correlation > 0: spatially close points tend to have similar vectors (positive ρ)
    correlation = 0: spatial and vector are independent (ρ ≈ 0)
    correlation < 0: nearby points tend to have dissimilar vectors (negative ρ)
    """
    locations = generate_spatial_distribution(n)  # Clustered lat/lon
    spatial_component = location_to_vector(locations, dim)  # e.g., RBF from lat/lon
    random_component = np.random.randn(n, dim)
    if correlation > 0:
        vectors = correlation * spatial_component + (1 - correlation) * random_component
    elif correlation == 0:
        vectors = random_component
    else:
        # Negative: subtract aligned part or use orthogonal block prototypes
        vectors = (-correlation) * orthogonalize_by_location(spatial_component) + (
            1 + correlation
        ) * random_component
    return locations, normalize(vectors)
```

**Validation:** On held-out pairs, compute **Spearman** correlation between **geo distance** and **vector distance** (or Kendall τ). Report **ρ̂** next to each synthetic run.

**Parameters to sweep:**

- `n`: 10K, 100K, 1M, 10M  
- `dim`: 128, 512, 768  
- `correlation`: **−0.75, −0.5, −0.25, 0.0, 0.25, 0.5, 0.75, 1.0** (or empirically calibrated ρ̂ bins)

### Legacy: single-parameter [0, 1] mix

Earlier drafts used only `correlation ∈ [0, 1]` (positive-only). For **negative ρ**, use the extended formulation above or dedicated **anti-aligned** generators.

### Controlled Selectivity Sweep

Generate datasets with varying spatial density to test different selectivity ranges:

```python
def generate_selectivity_dataset(n, dim, density_model):
    """density_model: 'uniform', 'clustered', 'skewed'"""
    ...
```

For each dataset, define spatial queries at specific selectivity levels:
- σ_s = 0.001, 0.01, 0.05, 0.10, 0.25, 0.50, 0.90

## Dataset Preparation Tooling

Create a data preparation script:

```
research/05_evaluation/scripts/
├── generate_synthetic.py      # Generate synthetic datasets
├── prepare_osm.py            # Download and process OSM data
├── prepare_yfcc.py           # Process YFCC100M subset
├── embed_text.py             # Generate text embeddings
├── load_to_starrocks.py      # Load data into StarRocks tables
└── generate_ground_truth.py  # Compute exact top-k for recall
```

## Ground Truth Generation

For recall measurement, pre-compute exact top-k results:

```python
def compute_ground_truth(dataset, queries, k):
    """Brute-force exact search for each query."""
    results = []
    for query_vector, query_region in queries:
        # Filter by spatial region
        candidates = [
            (i, vec) for i, (loc, vec) in enumerate(dataset)
            if region_contains(query_region, loc)
        ]
        # Compute exact distances
        distances = [(i, np.linalg.norm(vec - query_vector)) 
                     for i, vec in candidates]
        # Sort and take top-k
        distances.sort(key=lambda x: x[1])
        results.append(distances[:k])
    return results
```

## Storage Requirements

| Dataset | Records | Dim | Vector Size | Location Size | Total |
|---------|---------|-----|------------|---------------|-------|
| SIFT1M+geo | 1M | 128 | 512 MB | 16 MB | ~528 MB |
| OSM California | 1M | 512 | 2 GB | 16 MB | ~2 GB |
| YFCC10M | 10M | 512 | 20 GB | 160 MB | ~20 GB |
| Synthetic 10M | 10M | 128 | 5 GB | 160 MB | ~5 GB |
