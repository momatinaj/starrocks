# Phase 06 Design Notes

## Validation Philosophy

The package is not considered mature unless it is:

- correct
- explainable
- benchmarked against stable baselines
- regression-testable

## Validation Layers

### Layer 1: FE correctness

- planner recognition
- strategy selection
- EXPLAIN visibility

### Layer 2: BE correctness

- partition build/read
- repair behavior
- candidate merge correctness

### Layer 3: SQL integration

- end-to-end query semantics
- plan stability for representative query forms

### Layer 4: Performance

- microbenchmarks
- query-level latency/QPS
- build and compaction cost

### Layer 5: Lifecycle

- behavior across segment creation and compaction
- interactions with deletes/tombstones as applicable to StarRocks semantics

## Primary Test And Bench Anchors

- `be/test/storage/index/vector_index_test.cpp`
- `be/test/storage/index/vector_search_test.cpp`
- `fe/fe-core/src/test/java/com/starrocks/planner/VectorIndexTest.java`
- `fe/fe-core/src/test/java/com/starrocks/analysis/VectorIndexTest.java`
- `test/sql/test_vector_index/`
- `be/src/bench/`

## Rejected Alternatives

### Rejected: benchmark-only validation

Reason:

- strong latency numbers without correctness and regression coverage are not useful

### Rejected: correctness-only validation

Reason:

- this feature exists specifically to improve performance, so missing benchmark proof would defeat the purpose

## Output Expectations

- a final package validation contract
- a reusable result-recording pattern under `results/`
- clear handoff from planning to future implementation turns
