# Phase 06 Test Plan

## FE Unit Tests

Validate:

- strategy selection logic
- option propagation
- EXPLAIN output
- DDL/index-property validation if hybrid index syntax is introduced

Likely anchors:

- `fe/fe-core/src/test/java/com/starrocks/planner/VectorIndexTest.java`
- `fe/fe-core/src/test/java/com/starrocks/analysis/VectorIndexTest.java`
- `fe/fe-core/src/test/java/com/starrocks/sql/analyzer/AnalyzeVectorIndexDMLTest.java`

## BE Unit Tests

Validate:

- partition build/read
- tiny partition behavior
- boundary repair toggling
- merge correctness
- lifecycle assumptions around compaction-oriented rebuilds

Likely anchors:

- `be/test/storage/index/vector_index_test.cpp`
- `be/test/storage/index/vector_search_test.cpp`

## SQL Integration Tests

Validate:

- supported spatial+vector query forms
- EXPLAIN output and strategy visibility
- broad/selective/boundary-heavy cases
- planner fallback vs partitioned vs repaired behavior

Primary anchor:

- `test/sql/test_vector_index/` or a dedicated hybrid suite

## Benchmark Validation

- microbenchmarks under `be/src/bench/`
- baseline comparisons
- ablation comparisons
- scale tests
- regression replay of representative benchmark cases

## Regression Risks

- performance wins that regress correctness silently
- plan instability across similar query forms
- build/read lifecycle costs hidden from benchmark reports

## Exit Conditions

- every phase output has a corresponding validation path
- all package layers are represented: FE, BE, SQL, benchmark, lifecycle
