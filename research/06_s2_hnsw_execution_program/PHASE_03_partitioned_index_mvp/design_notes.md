# Phase 03 Design Notes

## MVP Definition

The MVP is strictly:

- S2 spatial partitioning
- one HNSW per qualifying partition
- partition-local search
- global merge

It explicitly excludes:

- cross-partition recovery
- overlap/replication
- adaptive planner strategy

## Primary Reference Files

- `be/src/storage/rowset/segment_writer.cpp`
- `be/src/storage/rowset/array_column_writer.cpp`
- `be/src/storage/rowset/segment_iterator.cpp`
- `be/src/storage/index/vector/vector_index_writer.cpp`
- `be/src/storage/index/vector/tenann_index_reader.cpp`
- `be/src/geo/geo_types.h`

## MVP Design Decisions

### Partition key

- Use S2 cell ID as the partition key
- Keep S2 level fixed for the MVP so the ablation stays interpretable

### Tiny partitions

- below a configured threshold, do not build HNSW
- keep exact/brute-force search behavior for those partitions

### Candidate merge

- search each matching partition independently
- merge candidates by final vector distance
- exact spatial verification still applies for boundary cells even in the MVP

### Query routing

- use S2 covering of the query region
- route only to matching partitions

## Rejected Alternatives

### Rejected: adaptive S2 level in MVP

Reason:

- it would blur whether gains come from partitioning or adaptive granularity

### Rejected: add repair immediately

Reason:

- would destroy the `A2` partition-only ablation

## Open Design Questions For Later Phases

- whether hybrid index metadata should share the current vector index file contract
- whether future repair should be stitching or overlap first
