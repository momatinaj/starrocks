# Expected Baselines

## Baseline Approaches

These baselines represent the naive approaches that our hybrid index must outperform to justify its implementation complexity.

### Baseline 1: Sequential Scan (No Index)

```
For each row in segment:
    if ST_Contains(region, row.location):
        compute distance(row.embedding, query_vector)
        maintain top-k heap
Return top-k
```

**Expected performance**:
- Latency: O(n · D) — linear in segment size × vector dimension
- For 10K vectors, D=512: ~5-10ms (memory-bound, good cache behavior)
- For 100K vectors, D=512: ~50-100ms
- For 1M vectors, D=512: ~500-1000ms
- Recall: 1.0 (exact)

**When competitive**: Very selective spatial filters (σ_s < 0.1%) where only 10-100 rows survive.

### Baseline 2: Spatial Filter → Brute-Force ANN

```
qualifying_rows = []
For each row in segment:
    if ST_Contains(region, row.location):
        qualifying_rows.append(row)

Sort qualifying_rows by distance(embedding, query_vector)
Return top-k
```

**Expected performance**:
- Latency: O(n · C_spatial + σ_s · n · D)
- For σ_s=1%, n=100K, D=512: ~10ms spatial scan + ~5ms vector distances = ~15ms
- For σ_s=10%, n=100K, D=512: ~10ms spatial scan + ~50ms vector distances = ~60ms
- Recall: 1.0 (exact)

### Baseline 3: HNSW → Spatial Post-Filter

```
# Oversample: request K' = k / σ_s candidates from HNSW
candidates = HNSW_search(query_vector, K')
filtered = [c for c in candidates if ST_Contains(region, c.location)]
Return top-k from filtered
```

**Expected performance**:
- Latency: O(K' · log(n) · M · C_hop)
- Problem: Must estimate σ_s to choose K'. If σ_s estimate is wrong:
  - Underestimate: too few candidates after filtering, low recall
  - Overestimate: too many HNSW hops, slow
- For k=10, σ_s=1%, K'=1000: ~5ms HNSW search + ~1ms spatial filter = ~6ms
- For k=10, σ_s=0.1%, K'=10000: ~50ms HNSW search (exceeds efSearch budget)
- Recall: depends on K'/k ratio and spatial distribution

### Baseline 4: HNSW with Delete Filter (Cascade)

```
qualifying_ids = spatial_scan(region)  # Get qualifying row IDs
candidates = HNSW_search_with_filter(query_vector, k, qualifying_ids)
Return candidates
```

**Expected performance**:
- Latency: O(n · C_spatial) + O(k/σ_s · log(n) · M · C_hop)
- The HNSW search degrades because the delete filter causes many "dead ends" in the graph
- For σ_s=1%, k=10, n=100K: ~10ms spatial + ~20-50ms degraded HNSW = ~30-60ms
- For σ_s=10%, k=10, n=100K: ~10ms spatial + ~5-10ms HNSW = ~15-20ms
- Recall: ~0.85-0.95 (depends on graph connectivity after filtering)

## Expected Improvement from Hybrid Index

| Selectivity | Best Baseline | Baseline Latency | Hybrid Target | Speedup Target |
|-------------|--------------|------------------|---------------|----------------|
| < 0.1% | Brute-force on filtered | ~2-5ms | ~2-5ms | ~1x (comparable) |
| 0.1-1% | Cascade | ~30-60ms | ~5-10ms | ~5x |
| 1-10% | Cascade | ~15-30ms | ~3-8ms | ~3-5x |
| 10-50% | HNSW+post-filter | ~10-20ms | ~5-10ms | ~2x |
| > 50% | HNSW+post-filter | ~5-10ms | ~5-10ms | ~1x (comparable) |

The hybrid index provides the most value in the **0.1-10% selectivity range**, which corresponds to typical city-scale and neighborhood-scale spatial queries — the most common use case for geo+vector search.

## Benchmark Protocol

1. **Warm-up**: Run 100 queries to warm caches
2. **Measurement**: Run 1000 queries, record per-query latency
3. **Metrics**: p50, p95, p99 latency, QPS, Recall@K
4. **Ground truth**: Brute-force exact search for recall computation
5. **Repeatability**: 3 runs, report median
