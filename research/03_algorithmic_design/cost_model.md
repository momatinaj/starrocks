# Cost Model for Strategy Selection

## Overview

A critical finding from both the literature and industry surveys is that the optimal query strategy depends on the **spatial selectivity** — the fraction of data that passes the spatial filter. **Additionally**, the alignment between geography and embeddings (**spatial–vector correlation ρ**) changes which strategies are optimal (see [spatial_vector_correlation.md](spatial_vector_correlation.md)).

## Definitions

- **σ_s**: Spatial selectivity = |rows in spatial region| / |total rows in segment|
- **ρ̂**: Estimated **spatial–vector correlation** (segment/table level): are nearby points similar in embedding space? (Positive / ≈0 / negative)
- **n**: Total rows in segment
- **k**: Requested top-k results
- **D**: Vector dimensionality
- **M**: HNSW max connections per node
- **C_dist**: Cost of one vector distance computation
- **C_spatial**: Cost of one spatial containment check (S2 cell check or ST_Contains)
- **C_hop**: Cost of one HNSW graph hop (memory access + distance computations)

## Strategy Cost Estimates

### Strategy 1: Brute-Force on Filtered Subset

```
Cost_BF = σ_s · n · (C_spatial + C_dist) + k · log(k)
```

Best when σ_s is very small (few candidates to scan).

### Strategy 2: Cascade (Spatial Filter → HNSW with Delete Filter)

```
Cost_Cascade = n · C_spatial + HNSW_search_cost(n, k, σ_s)
```

Where HNSW search cost degrades as σ_s decreases (more nodes filtered, more hops needed):
```
HNSW_search_cost ≈ (k / σ_s) · log(n) · M · C_hop
```

Best for moderate selectivity where HNSW graph is still navigable.

### Strategy 3: Filtered HNSW (ACORN-style)

```
Cost_ACORN = ACORN_search_cost(n, k, σ_s)
```

Where ACORN cost depends on strategy:
```
If σ_s < 0.08: Cost = k · log(n) · M² · C_hop    (blind 2-hop)
If σ_s < 0.40: Cost = k · log(n) · M · α · C_hop  (directed 2-hop, α ≈ M/2)
If σ_s ≥ 0.40: Cost = k · log(n) · M · C_hop      (standard 1-hop)
```

### Strategy 4: Spatial-Partitioned HNSW

```
Cost_Partitioned = C_s2_covering + Σ_i HNSW_search_cost(n_i, k, 1.0)
```

Where `n_i` is the size of partition i, and sum is over partitions overlapping the spatial region. Selectivity within each partition is ~1.0 (all nodes qualify).

Best when spatial query maps to a small number of partitions.

## Correlation-Aware Adjustments (ρ̂)

The following **does not replace** the σ_s tree below; it **modifies** branch choices and parameters.

### Vector-first oversampling factor (F)

When using ANN **before** spatial filter, required candidate count is roughly **F·k** where **F ≥ 1/σ_s** in the independent case. Correlation adjusts **effective** filter pass-through:

| ρ̂ | Effect on vector-first | Suggested F (heuristic) |
|---|------------------------|-------------------------|
| **Positive** | ANN neighbors more likely inside region | **Lower** than 1/σ_s (still ≥ 1; validate by recall) |
| **≈ 0** | No locality bonus | **F ≈ 1/σ_s** (or higher for recall target) |
| **Negative** | ANN may rank **global** neighbors that fail the region | **Higher** than 1/σ_s; prefer **spatial-first** or **filtered ANN** |

### Spatial-partitioned HNSW

| ρ̂ | Effect | Mitigation |
|---|--------|------------|
| **Positive** | Partitions align with vector neighborhoods; **high** within-partition recall | Default build |
| **≈ 0** | Partitions are **only** for pruning; within-partition graph is standard HNSW | Still useful; tune **S2 level** by **σ_s** and density |
| **Negative** | **Boundary** cuts may separate true k-NN in vector space | **Cross-partition stitching**; **raise efSearch**; or **prefer filtered single-graph HNSW** |

### Filtered HNSW (ACORN)

| ρ̂ | Effect |
|---|--------|
| **Positive** | Fewer **dead ends** when restricting to region (subgraph stays connected) |
| **≈ 0** | **Baseline** robust strategy; matches “no structure” assumption |
| **Negative** | Subgraph may be **poorly connected** → prefer **blind 2-hop** / **higher efSearch** |

## Strategy Selection Decision Tree

```
estimate σ_s (spatial selectivity)
│
├── σ_s < 0.001 (< 0.1%)
│   └── Strategy 1: Brute-force on filtered subset
│       Reasoning: So few candidates that graph traversal overhead isn't justified
│
├── 0.001 ≤ σ_s < 0.08 (0.1% - 8%)
│   ├── If spatial-partitioned index exists:
│   │   └── Strategy 4: Search matching partitions
│   │       Reasoning: Small number of partitions, full graph connectivity within each
│   └── If only filtered HNSW:
│       └── Strategy 3 (blind 2-hop): ACORN with aggressive exploration
│           Reasoning: Graph connectivity degraded, need wide exploration
│
├── 0.08 ≤ σ_s < 0.40 (8% - 40%)
│   └── Strategy 3 (directed 2-hop): ACORN with targeted exploration
│       Reasoning: Moderate filtering, directed exploration sufficient
│
├── 0.40 ≤ σ_s < 0.90 (40% - 90%)
│   └── Strategy 2 or 3 (standard HNSW with post-filter)
│       Reasoning: Most nodes pass filter; pre-filtering overhead not justified
│
└── σ_s ≥ 0.90 (> 90%)
    └── Standard HNSW, spatial filter as post-check
        Reasoning: Nearly all nodes qualify; ignore spatial during search
```

## Selectivity Estimation

### Option 1: S2 Cell Statistics (Recommended)

At index build time, compute and store:
- Count of vectors per S2 cell at the partition level
- Total vector count per segment

At query time:
```
1. Compute S2 cell covering of spatial region R
2. Sum counts of covering cells
3. σ_s = sum / total_segment_count
```

Cost: O(|covering cells|) — typically 10-100 cell lookups.
Accuracy: Approximate (cells partially overlapping R are counted fully).

### Option 2: Zone Map Statistics

If location data has zone maps (min/max lat/lon per page):
```
1. Check which pages' spatial ranges overlap the query region
2. σ_s = overlapping_pages / total_pages
```

Cost: O(pages) — existing infrastructure in StarRocks.
Accuracy: Coarse, but fast.

### Option 3: Sampling

```
1. Sample S random rows from segment
2. Evaluate spatial predicate on each
3. σ_s = passing_count / S
```

Cost: O(S · C_spatial).
Accuracy: Depends on sample size, may be slow for expensive spatial predicates.

**Recommendation**: Use S2 cell statistics (Option 1) as primary, zone maps (Option 2) as fallback.

## Cost Model Parameters

These parameters should be configurable and tunable:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `brute_force_threshold` | 0.001 | Selectivity below which brute-force is used |
| `acorn_blind_threshold` | 0.08 | Selectivity below which blind 2-hop is used |
| `acorn_directed_threshold` | 0.40 | Selectivity below which directed 2-hop is used |
| `post_filter_threshold` | 0.90 | Selectivity above which post-filtering is used |
| `partition_min_size` | 100 | Minimum rows per spatial partition for HNSW |

## Integration with StarRocks Query Planner

The cost model should be evaluated in the FE query planner (not at BE query time):

```
RewriteToVectorPlanRule (existing)
└── Extended with spatial awareness:
    1. Detect spatial predicate in WHERE clause
    2. Estimate spatial selectivity using table/column statistics
    3. Choose strategy and encode in VectorSearchOptions
    4. BE executes chosen strategy
```

This avoids per-segment strategy selection overhead and allows the planner to make globally optimal decisions.

## Open Questions

- Should strategy selection be per-segment (different segments may have different data distributions)?
- How to handle the transition between strategies smoothly (avoid performance cliffs at threshold boundaries)?
- What is the overhead of selectivity estimation, and is it worth the cost for small queries?
- How to **estimate ρ̂** cheaply at **build/compaction** time (sampling, sketching), and how often to **refresh** it?
- Can we **auto-detect** **negative ρ** (e.g., urban POI mix) and **default** to **stitching + filtered ANN** without user hints?
