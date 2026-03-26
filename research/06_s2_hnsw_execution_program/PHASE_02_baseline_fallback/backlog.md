# Phase 02 Backlog

## Epics

### E1: FE Pattern Recognition

- recognize spatial predicate + ANN top-k query shapes
- define supported first-wave predicates:
  - `ST_Contains`
  - point/radius style distance filter
  - polygon-based filters if represented through existing geo functions

### E2: FE/BE Option Propagation

- identify required extensions to vector search options
- define fallback strategy markers for thrift/runtime

### E3: BE Fallback Runtime

- spatial-first candidate restriction
- ANN path reuse where beneficial
- brute-force fallback for tiny candidate sets

## User Stories

### US1

As a query planner developer, I want spatial+vector queries recognized before the future hybrid index lands, so we have a correct and explainable baseline.

### US2

As a benchmarker, I want a fallback baseline that is better than naive vector-first post-filtering but still clearly distinct from partitioned HNSW.

### US3

As a future implementer, I want this phase to establish option propagation paths that later phases can extend rather than replace.

## Technical Subtasks

- inspect how `RewriteToVectorPlanRule` currently extracts vector predicates and ordering
- define where spatial predicate info should enter FE option objects
- define how runtime distinguishes:
  - exact brute-force on filtered rows
  - filtered ANN fallback
  - existing vector path
- define what EXPLAIN should show for fallback mode

## Dependencies

- Phase 01 benchmark contract
- existing vector rewrite and runtime plumbing

## Blockers To Watch

- unsupported spatial function forms in the first planner slice
- ambiguity in whether fallback is planner-selected or runtime-selected

## Exit Artifacts

- clear FE/BE path for fallback
- test plan for planner, BE, and SQL coverage
- benchmark spec for `B2`
