# Phase 01 Test Plan

## Objective

Validate that the planning program itself is complete and points to the right existing StarRocks validation surfaces.

## Test Categories

### Document consistency checks

- verify every phase directory contains the expected five documents
- verify all root artifacts reference the same baseline and ablation IDs
- verify all file-path references point to real StarRocks locations

### Future test anchor checks

Record the test surfaces later phases should extend:

- BE: `be/test/storage/index/vector_index_test.cpp`
- BE: `be/test/storage/index/vector_search_test.cpp`
- FE: `fe/fe-core/src/test/java/com/starrocks/planner/VectorIndexTest.java`
- FE: `fe/fe-core/src/test/java/com/starrocks/analysis/VectorIndexTest.java`
- SQL: `test/sql/test_vector_index/`

## Regression Risks To Track

- inconsistent naming of baselines and ablations across documents
- unclear feature boundaries between fallback, partitioning, and repair
- phase docs that accidentally redefine metrics or benchmark axes

## Exit Conditions

- all phase docs use a consistent template
- future implementation phases can reference one stable test and benchmark contract
