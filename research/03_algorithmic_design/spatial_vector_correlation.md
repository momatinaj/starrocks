# Spatial–Vector Correlation

## Why This Matters

Hybrid GIS+ANN performance depends not only on **spatial selectivity** (how many rows fall in the region) but on **how aligned** geographic proximity is with embedding similarity.

- If **nearby points tend to be similar in vector space**, spatial partitioning and spatial-first pipelines align with how HNSW already clusters neighbors.
- If **location and embeddings are independent**, spatial structure gives **no free lunch** for the vector graph: filtered ANN and careful oversampling dominate.
- If **nearby points tend to be dissimilar in vector space**, pure vector graphs may connect points across space while spatial partitions **cut true nearest neighbors**, hurting recall unless you add cross-partition edges or bias the plan toward vector-first exploration.

This document defines three scenarios and maps **which index / execution strategy is strongest** in each.

## Definitions

### Informal

| Scenario | Intuition | Example workloads |
|----------|-----------|-------------------|
| **Positive ρ** | “Things that look alike are also near each other on the map.” | Same-neighborhood storefront photos; local signage with similar appearance; terrain patches from aerial tiles. |
| **Zero ρ** | “Location and embedding are unrelated.” | Embeddings from global text models assigned random-ish geo; mixed catalog with arbitrary placement. |
| **Negative ρ** | “Neighbors on the map are often different in embedding space.” | Dense urban mix (diverse POIs per block); adversarial or diversified placement; embeddings that encode non-local attributes. |

### Operational definition (for estimation)

Let **d_geo(i,j)** be geographic distance (e.g., great-circle meters) and **d_vec(i,j)** be embedding distance (e.g., L2 or cosine distance).

On a **sample of pairs** (random, k-NN in space, or k-NN in vector space), compute a **rank correlation** between:

- ranks of **d_geo** and **d_vec** (Spearman), or  
- Pearson correlation of **−d_geo** and **−d_vec** (higher similarity when both small).

**ρ** (conceptual):

- **ρ ≫ 0**: small **d_geo** → tends to small **d_vec** (positive correlation).
- **ρ ≈ 0**: no monotonic relationship.
- **ρ ≪ 0**: small **d_geo** → tends to **large** **d_vec** (negative correlation).

**Note:** This is a **dataset-level** (or segment-level) statistic; it can vary by table, region, or time. The planner can treat **ρ̂** (estimated ρ) as a **hint** for strategy, not a hard guarantee.

## Effect on Naive Strategies

### Vector-first (ANN then spatial filter)

- **Positive ρ**: Top ANN candidates are **more likely** to land inside the spatial region → **lower oversampling factor** needed for the same recall@K.
- **Zero ρ**: ANN candidates are **uncorrelated** with being inside the region → **high oversampling** (worst case for vector-first).
- **Negative ρ**: ANN aggressively follows vector edges; neighbors in space may be **far in vector space** → graph hops may **not** stay inside the region; **oversampling and recall risk** are often **worse** than zero correlation unless the filter is very selective.

### Spatial-first (region then ANN)

- **Positive ρ**: Survivors of the spatial filter are **relatively coherent** in vector space → brute-force or small-graph ANN on survivors is **efficient**; partitions align with vector neighborhoods.
- **Zero ρ**: Spatial filter only **reduces cardinality**; survivors are still **hard** for a single HNSW graph that was built without spatial awareness → **filtered ANN** (ACORN) or **partition-local HNSW** still needed; no bonus from “spatial locality” in vector space.
- **Negative ρ**: Survivors may be **diverse** in embedding space; **no** inherent benefit from spatial clustering. **Boundary** effects dominate: true top-k may **straddle** spatial partition boundaries.

## Best Index / Strategy by Scenario

The table below is **guidance** for design and tuning; always validate with benchmarks on your data.

| Scenario | Preferred index / execution | Why | Watch-outs |
|----------|----------------------------|-----|------------|
| **Positive ρ** | **Spatial-partitioned HNSW** (S2 per partition); **spatial-first cascade**; optional **lighter** ACORN / 2-hop | Partitions and traversal match **joint** structure of space + vectors. | If ρ is only **locally** positive (e.g., per city), use **segment/tablet** stats or hierarchical S2 level. |
| **Zero ρ** | **Filtered HNSW (ACORN)**; **IVF²**-style dual index; **selectivity-driven** plan (ignore spatial in graph when σ_s high) | No structural alignment: must rely on **predicate-aware** ANN and **statistics**, not spatial locality in the graph. | **Vector-first** needs **large** oversample; **spatial-partitioned** HNSW still helps **pruning** but **does not** improve within-partition graph quality. |
| **Negative ρ** | **Cross-partition stitching** (StitchedVamana-style); **stronger** ACORN 2-hop / **higher efSearch**; **avoid** relying on partition-only search without boundary fix | Pure spatial cuts may **separate** true nearest neighbors in vector space. | **Learned** indexes (LIST-style) or **R-tree + HNSW** may help if geometry is complex; **cost model** should **prefer** strategies that **don’t assume** spatial locality of vectors. |

### Summary matrix

| | **Positive ρ** | **Zero ρ** | **Negative ρ** |
|---|----------------|------------|----------------|
| **Spatial-partitioned HNSW** | **Strong** — partitions match vector neighborhoods | **OK for pruning** — graph quality per-partition is **neutral** | **Risky** — boundary cuts; **needs stitching** or **extra hops** |
| **Filtered HNSW (metadata only)** | **Good** | **Good** — **default** when structure unknown | **Good** but tune **efSearch** / 2-hop **up** |
| **Cascade (spatial → ANN on subset)** | **Strong** | **Depends on σ_s** | **σ_s small** OK; **large region** + diverse vectors → **expensive** brute-force phase |
| **Vector-first + post-filter** | **Least bad** among vector-first | **Weak** | **Weakest** |
| **Hybrid graph (spatial + vector edges)** | **Potential win** if edges encode both | **Hard to justify** | **Most relevant** — **spatial edges** can **repair** bad vector-only paths |

## Interaction with Selectivity (σ_s)

Correlation and selectivity **multiply**:

- **High σ_s** (large region): correlation matters **more** for whether **spatial-first** returns a **manageable** set for ANN.
- **Low σ_s** (tiny region): **brute-force** on survivors often wins regardless of ρ; correlation mainly affects **recall** of approximate methods.

**Rule of thumb:**

1. Estimate **σ_s** first (region size vs. data).
2. If **σ_s** forces **small** candidate sets → **ρ** is secondary.
3. If **σ_s** is **moderate** and ANN is **required** → use **ρ̂** to choose **partitioned vs. filtered** + **boundary stitching**.

## Estimation in StarRocks (Future Work)

- **Build time**: sample pairs within random S2 cells; compute **ρ̂** per segment or table; store in **index metadata** or **statistics**.
- **Query time**: optional **session** or **table property** `correlation_hint = {positive|neutral|negative}` for power users.
- **Planner**: combine **σ̂_s**, **ρ̂**, and **k** to pick **spatial-first vs. vector-first vs. hybrid** and **efSearch** / **partition list**.

## References

- Synthetic **correlation** control: see [datasets.md](../../05_evaluation/datasets.md) (`correlation` parameter).
- **LIST** (VLDB 2024): learning when **spatial + textual** structure is **jointly** predictable.
- **KHI**: attribute-space partitioning when **vector and attributes** (including **spatial bins**) **jointly** constrain search.

## Open Questions

1. Can we **cheaply** estimate **ρ̂** per **segment** during **compaction** without full pairwise sampling?
2. Does **negative ρ** correlate with **specific** domains (e.g., urban POIs) **strongly enough** to warrant a **default** profile?
3. Should **cross-partition edges** be **always on** or **only when ρ̂ < threshold**?
