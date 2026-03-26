# Phase 03 Test Plan

## BE Unit Tests

Primary focus:

- partition metadata creation
- per-partition HNSW build success
- read path resolves correct partitions
- merge logic returns valid top-k ordering
- tiny partitions fall back as designed

Likely anchors:

- `be/test/storage/index/vector_index_test.cpp`
- `be/test/storage/index/vector_search_test.cpp`

## FE Tests

For MVP, FE validation remains narrow:

- DDL/index metadata validation if new syntax or index properties are introduced
- runtime plan surfaces expected metadata to BE

Likely anchors:

- `fe/fe-core/src/test/java/com/starrocks/analysis/VectorIndexTest.java`
- `fe/fe-core/src/test/java/com/starrocks/planner/VectorIndexTest.java`

## SQL Tests

Add later coverage for:

- partitioned-index-backed spatial+vector queries
- EXPLAIN showing partition-aware behavior if visible
- tiny-partition fallback cases

Primary anchor:

- `test/sql/test_vector_index/` or a new hybrid test directory

## Regression Risks

- wrong cell routing silently drops true nearest neighbors
- merge logic returns locally-good but globally-bad results
- tiny partition fallback creates inconsistent behavior between sparse and dense regions

## Exit Conditions

- `A2` can be tested independently from boundary repair
- partition correctness and merge correctness are both covered
