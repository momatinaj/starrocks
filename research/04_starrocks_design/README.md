# StarRocks-Specific Design

## Overview

This section maps the hybrid GIS+Vector index research to concrete design decisions within the StarRocks codebase. The design must:

1. Fit StarRocks' segment-based storage architecture
2. Integrate with the existing `IndexType` enum and factory pattern
3. Leverage the existing S2 library and TenANN/HNSW
4. Work with the `RewriteToVectorPlanRule` optimizer
5. Support both shared-nothing and shared-data deployment modes

## Design Documents

| Document | Purpose |
|----------|---------|
| [Architecture Fit](architecture_fit.md) | How the hybrid index maps to StarRocks internals |
| [Index Type Design](index_type_design.md) | New IndexType, file format, metadata |
| [Query Planner Changes](query_planner_changes.md) | FE optimizer rule modifications |
| [Segment Integration](segment_integration.md) | BE segment writer/reader changes |
| [SQL Syntax](sql_syntax.md) | CREATE INDEX and query syntax |
| [S2 Integration](s2_integration.md) | Leveraging the existing S2 library |
| [TenANN Extensions](tenann_extensions.md) | Extending or replacing TenANN |
| [Implementation Roadmap](implementation_roadmap.md) | Phased delivery plan |

## Key Design Decision

After evaluating five candidate approaches (see `03_algorithmic_design/`), the recommended design is a **tiered implementation**. **Spatial–vector correlation:** Tier 2 (spatial-partitioned HNSW) is **strongest when ρ̂ ≥ 0**; for **ρ̂ ≪ 0**, prefer **filtered HNSW** and/or **cross-partition stitching** — see [spatial_vector_correlation.md](../03_algorithmic_design/spatial_vector_correlation.md).

### Tier 0: Cascade Pipeline (Weeks 1-4)
Minimal changes: optimizer detects spatial+vector pattern, evaluates spatial filter first, passes qualifying row IDs to existing HNSW reader via delete filter.

### Tier 1: Filtered HNSW with S2 Metadata (Weeks 5-12)
Augment HNSW nodes with S2 cell IDs. Implement ACORN-style traversal with S2 cell short-circuit optimization. Add selectivity-based strategy selection.

### Tier 2: Spatial-Partitioned HNSW (Weeks 13-24)
New hybrid index type. S2 cell partitioning within segments. Per-partition HNSW graphs. Cross-partition stitching. Full cost model integration.

Each tier provides measurable value and is independently shippable.
