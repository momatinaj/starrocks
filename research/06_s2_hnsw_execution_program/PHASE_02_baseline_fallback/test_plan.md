# Phase 02 Test Plan

## FE Tests

Extend planner/analyzer coverage to prove:

- supported spatial+vector query shapes are recognized
- unsupported shapes do not silently route into the fallback path
- explain output reflects fallback strategy

Primary anchors:

- `fe/fe-core/src/test/java/com/starrocks/planner/VectorIndexTest.java`
- `fe/fe-core/src/test/java/com/starrocks/analysis/VectorIndexTest.java`

## BE Tests

Validate:

- propagated fallback options are interpreted correctly
- small filtered sets route to exact distance mode
- non-trivial filtered sets can route to ANN reuse mode

Primary anchors:

- `be/test/storage/index/vector_search_test.cpp`
- scan-related tests if needed under `be/test/exec/`

## SQL Tests

Add a dedicated suite later to verify:

- accepted query forms
- stable EXPLAIN signatures
- planner fallback behavior on representative spatial predicates

Primary anchor:

- `test/sql/test_vector_index/`

## Regression Risks

- planner accidentally treats arbitrary non-spatial predicates as spatially special
- fallback path changes current vector behavior for non-spatial queries
- option propagation mismatches between FE and BE

## Exit Conditions

- FE, BE, and SQL test categories are each represented in the phase plan
- no ambiguity remains about how `B2` is validated
