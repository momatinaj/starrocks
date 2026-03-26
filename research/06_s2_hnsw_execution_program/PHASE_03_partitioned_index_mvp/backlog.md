# Phase 03 Backlog

## Epics

### E1: Hybrid Index Metadata

- define partition metadata persisted per segment
- define S2 cell to partition mapping
- define row-id and vector payload linkage per partition

### E2: Build Path

- compute S2 cells from spatial values during segment write
- group vectors into partitions
- build one HNSW per eligible partition
- define behavior for tiny partitions

### E3: Read Path

- map spatial query region to S2 cells
- locate matching partitions
- search partitions
- merge candidates into global top-k

## User Stories

### US1

As an index developer, I want partitioning logic defined independently from repair logic so I can benchmark the true benefit of spatial partitioning alone.

### US2

As a storage engineer, I want the MVP to fit the immutable-segment lifecycle so later compaction support is natural rather than bolted on.

### US3

As a benchmark owner, I want `A2` to be a stable partition-only ablation that later phases can compare against.

## Technical Subtasks

- define whether MVP uses a new hybrid index family or an extension of vector index storage
- define how partition metadata is read quickly at query startup
- define exact behavior for partitions below HNSW threshold
- define top-k merge semantics when each partition returns local candidates

## Dependencies

- Phase 02 fallback baseline
- S2 geometry support in `be/src/geo/`
- existing vector build/read path through TenANN

## Blockers To Watch

- DDL/index metadata ambiguity between existing vector index and new hybrid index
- partition fan-out becoming too large at poorly chosen S2 levels

## Exit Artifacts

- MVP design clear enough to implement
- test and benchmark plan sufficient for `A2`
