# Metrics

## Primary Metrics

### Recall@K

**Definition**: Fraction of true top-k results returned by the approximate search.

```
Recall@K = |ANN_results ∩ Exact_results| / K
```

Where:
- `ANN_results`: set of K row IDs returned by hybrid index search
- `Exact_results`: set of K row IDs from brute-force exact search (ground truth)

**Why it matters**: The hybrid index is approximate (due to HNSW). Recall measures how much accuracy we sacrifice for speed.

**Target**: ≥ 0.95 for all selectivity ranges.

**Measurement**: Compare against pre-computed ground truth for each query.

**Correlation sensitivity:** Report Recall@K **separately** for datasets with **positive**, **near-zero**, and **negative** **spatial–vector correlation** (ρ). The same index may hit recall targets in one regime and fail in another (e.g., spatial-partitioned HNSW at **negative ρ** without stitching). See [spatial_vector_correlation.md](../03_algorithmic_design/spatial_vector_correlation.md).

### Query Latency

**Definition**: Wall-clock time from query submission to result delivery.

| Percentile | Description | Target |
|------------|-------------|--------|
| p50 | Median latency | < 10 ms |
| p95 | Tail latency | < 30 ms |
| p99 | Worst-case latency | < 100 ms |

**Measurement**: Record per-query latency for 1000+ queries after warm-up.

### Queries Per Second (QPS)

**Definition**: Number of queries processed per second under sustained load.

**Measurement**: 
- Single-thread QPS: sequential queries, measure throughput
- Multi-thread QPS: parallel queries (1, 4, 8, 16 threads), measure aggregate throughput

**Target**: ≥ 1000 QPS single-thread for 10K vectors/segment.

## Secondary Metrics

### Index Build Time

**Definition**: Wall-clock time to build the hybrid index for a segment.

| Segment Size | Target |
|-------------|--------|
| 10K vectors | < 1 s |
| 100K vectors | < 10 s |
| 1M vectors | < 60 s |

**Measurement**: Time the `HybridVectorIndexBuilder::finish()` call.

### Index File Size

**Definition**: Size of the `.hvi` file relative to the raw vector data.

```
Overhead ratio = .hvi file size / (N × D × 4 bytes)
```

**Target**: Overhead ratio < 2.0 (index smaller than 2x the raw vector data).

### Memory Usage

**Definition**: Peak RSS during index build and query.

**Components**:
- HNSW graph memory: N × M × 4 bytes per partition
- S2 Cell Index: P × 20 bytes (P = partition count)
- Vector data: N × D × 4 bytes (if held in memory)
- Spatial metadata: N × 16 bytes (S2 cell ID + lat/lon)

**Target**: < 1.5x the memory of standalone HNSW for the same data.

### Compaction Overhead

**Definition**: Additional time for segment compaction due to hybrid index rebuild.

**Measurement**: Compare compaction time with and without hybrid index.

## Selectivity-Specific Metrics

These metrics are measured at each selectivity level to characterize performance across the selectivity spectrum:

| σ_s Range | Label | Example Query |
|-----------|-------|---------------|
| < 0.001 | Micro | 100m radius, single block |
| 0.001-0.01 | Tiny | 1km radius, neighborhood |
| 0.01-0.05 | Small | 5km radius, district |
| 0.05-0.10 | Medium-Small | City center |
| 0.10-0.25 | Medium | Large city area |
| 0.25-0.50 | Medium-Large | Metro area |
| 0.50-0.90 | Large | State/province |
| > 0.90 | Full | Country/global |

For each selectivity level, report: Recall@K, p50/p95/p99 latency, QPS.

## Comparison Dimensions

### Against Baselines

| Baseline | What We Compare |
|----------|----------------|
| Sequential scan (no index) | Maximum possible speedup |
| Spatial filter → brute-force | Improvement from ANN within filtered set |
| HNSW → post-filter (oversampled) | Improvement from avoiding oversampling |
| Cascade (HNSW with delete filter) | Improvement from spatial-aware index |

### Against Approaches

| Approach | What We Compare |
|----------|----------------|
| Cascade Pipeline (Tier 0) | Baseline approach performance |
| Filtered HNSW (Tier 1) | ACORN-style traversal improvement |
| Spatial-Partitioned HNSW (Tier 2) | Full hybrid index performance |
| Combined (Tier 1+2) | Filtered + partitioned together |

### Across Parameters

| Parameter | Values to Sweep |
|-----------|----------------|
| Vector dimension | 128, 512, 768, 2048 |
| Segment size | 10K, 100K, 1M |
| HNSW M | 8, 16, 32 |
| HNSW efSearch | 50, 100, 200, 400 |
| S2 level | 10, 12, 14, 16 |
| k (top-k) | 1, 10, 50, 100 |

## Reporting Format

Results should be presented as:

1. **Recall-Latency curves**: For each approach, plot Recall@K vs p50 latency (varying efSearch)
2. **Selectivity-Latency curves**: For each approach, plot p50 latency vs spatial selectivity
3. **Speedup heatmap**: Speedup of hybrid index over cascade baseline, by selectivity × dimension
4. **Build time table**: Index build time by segment size × S2 level
5. **Memory table**: Peak memory by segment size × M × S2 level
