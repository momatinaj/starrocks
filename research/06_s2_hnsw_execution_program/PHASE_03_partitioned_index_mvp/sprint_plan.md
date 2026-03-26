# Phase 03 Sprint Plan

## Sprint Goal

Define the minimum viable **S2 spatial-partitioned HNSW** index that can build, read, and execute spatially-routed ANN queries per segment.

## Sprint Scope

### In scope

- hybrid index metadata and file-format slice
- segment build pipeline for S2 partitioning
- per-partition HNSW construction
- partition routing during query
- global top-k merge across matching partitions

### Out of scope

- boundary repair
- planner-driven adaptive choice among execution modes
- correlation-aware tuning

## Stories

### FE stories

- define the index-facing metadata expected from planner/runtime in MVP mode
- identify whether existing vector index DDL surfaces can be extended or mirrored

### BE stories

- create the partitioned build model
- define partition metadata persisted per segment
- implement reader routing from spatial region to partition list
- define merge semantics for partition-local candidate sets

### Test stories

- validate build/read lifecycle
- validate exact partition routing
- validate recall/latency comparisons against fallback

### Benchmarking stories

- establish `A2` as a reproducible benchmark target
- compare partitioned-only behavior to `B2`

## Acceptance Criteria

- partition-only design is fully specified
- MVP does not depend on repair or adaptive planner behavior
- `A2` is cleanly isolated as “S2 partitioning only”
