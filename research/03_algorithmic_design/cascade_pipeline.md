# Cascade Pipeline

## Core Idea

The simplest approach: evaluate the spatial filter first to produce a set of qualifying row IDs, then perform vector search only on those rows. No new index structure is needed — this composes existing capabilities.

## Architecture

```
Query: WHERE ST_Contains(polygon, location) ORDER BY distance(embedding, q) LIMIT k

Step 1: Spatial Filter
  ├── Option A: Sequential scan with ST_Contains evaluation
  ├── Option B: S2 cell covering → bitmap scan → ST_Contains refinement
  └── Output: BitSet of qualifying row IDs

Step 2: Vector Search on Filtered Subset
  ├── Option A: Brute-force distance computation on qualifying rows
  ├── Option B: HNSW search with delete-filter (skip non-qualifying nodes)
  └── Output: Top-k results

Step 3: Return Results
```

## Implementation in StarRocks (Minimal Changes)

### Option A: No Index Changes (Pure Cascade)

```
Optimizer Rule:
1. Detect pattern: ST_Contains/ST_Within/ST_DWithin + approx_l2_distance + ORDER BY + LIMIT
2. Push spatial predicate into segment scan
3. Collect qualifying row IDs
4. Pass qualifying row IDs as delete filter to HNSW index reader
5. HNSW search skips non-qualifying nodes
```

This leverages StarRocks' existing `del_id_filter` mechanism in the vector index reader.

### Option B: S2 Cell Bitmap Pre-filter

```
1. Add S2 cell ID column (or computed column) to table
2. Create BITMAP index on S2 cell ID column
3. At query time:
   a. Compute S2 cell covering of spatial region
   b. Use bitmap index to get qualifying row IDs (fast)
   c. Refine with exact ST_Contains (slower, but on smaller set)
   d. Pass qualifying row IDs to HNSW search
```

## Algorithm Detail

### Selectivity-Based Strategy

```
Input: spatial region R, query vector q, k

1. Estimate spatial selectivity σ_s (fraction of rows in R)
   → Can use S2 cell statistics or table statistics

2. If σ_s < 0.001:  (< 0.1% of data — tiny region)
     → Brute-force: scan only qualifying rows, compute exact distance
     → HNSW overhead not justified for so few candidates

   If 0.001 ≤ σ_s < 0.1:  (0.1% - 10%)
     → Spatial filter → brute-force ANN on survivors
     → Survivors are few enough for efficient linear scan

   If 0.1 ≤ σ_s < 0.5:  (10% - 50%)
     → Spatial filter → HNSW with delete filter
     → HNSW graph still navigable, filter applied during traversal

   If σ_s ≥ 0.5:  (> 50%)
     → Standard HNSW → post-filter
     → Most nodes pass filter; don't waste time pre-filtering
```

## Strengths

- **Simplest implementation**: Composes existing spatial functions + existing HNSW index
- **No new index structure**: Uses existing BITMAP index + existing VECTOR index
- **Full spatial predicate support**: Any ST_* function works as the filter
- **Risk-free**: No changes to core index infrastructure
- **Incremental**: Can be implemented as a first step, then replaced by hybrid index

## Weaknesses

- **Two-pass overhead**: Spatial filter and vector search are separate passes over the data
- **No spatial locality in HNSW**: The HNSW graph is not spatially aware, so filtered traversal may require many hops to find qualifying nodes
- **Poor cache behavior**: First pass (spatial) and second pass (vector) access different memory regions
- **Delete filter limitations**: StarRocks' delete filter mechanism may not efficiently handle large filtered sets
- **Brute-force bottleneck**: For moderate selectivity (1-10%), brute-force on survivors can be slow if many rows qualify

## Performance Expectations

| Spatial Selectivity | Cascade Pipeline Performance | vs. Hybrid Index (Expected) |
|--------------------|------------------------------|----------------------------|
| < 0.1% | Good (few candidates, brute-force fast) | Comparable |
| 0.1% - 1% | Moderate (brute-force on 1K-10K rows) | 2-3x slower |
| 1% - 10% | Moderate (HNSW with sparse filter) | 3-10x slower |
| 10% - 50% | Poor (HNSW traversal degraded by filter) | 5-20x slower |
| > 50% | Good (standard HNSW, light post-filter) | Comparable |

## Implementation Roadmap

1. **Phase 1**: Implement optimizer rule to detect spatial+vector query pattern and rewrite to cascade
2. **Phase 2**: Add S2 cell bitmap pre-filtering for faster spatial evaluation
3. **Phase 3**: Add selectivity estimation to choose between brute-force and HNSW+filter
4. **Phase 4**: Replace cascade with true hybrid index (if benchmarks justify it)

## Verdict

The cascade pipeline is the **recommended first step**. It provides immediate value with minimal risk and serves as the performance baseline for evaluating more sophisticated hybrid index approaches. All subsequent approaches should demonstrate meaningful improvement over this baseline to justify their implementation complexity.
