# Phase 02 Sprint Plan

## Sprint Goal

Define and prepare the minimal production-safe path for spatial+vector queries without introducing a new hybrid index yet.

## Sprint Scope

### In scope

- FE detection of spatial + ANN query patterns
- propagation of fallback search options to BE
- BE runtime strategy for spatial-first filtering and ANN/brute-force fallback
- baseline explainability and SQL test coverage

### Out of scope

- S2 partitioned index metadata
- new index file format
- boundary repair

## Stories

### FE stories

- detect supported spatial predicates in the vector rewrite phase
- enrich option structures for fallback execution mode
- define EXPLAIN visibility for the chosen fallback strategy

### BE stories

- consume propagated options in scan/runtime layers
- route to brute-force or filtered ANN behavior depending on selectivity assumptions
- keep the path implementation-friendly for later planner adaptation

### Test stories

- FE planner tests for recognized and rejected query patterns
- BE correctness tests for fallback selection semantics
- SQL tests proving the query plan is stable and explainable

### Benchmarking stories

- compare `B2` against `B0` and `B1`
- establish the immediate no-new-index baseline for all later phases

## Acceptance Criteria

- fallback path is defined precisely enough to implement in a later turn
- FE and BE responsibilities are separated clearly
- test plan covers correctness, planner visibility, and SQL behavior
- benchmark spec can isolate `B2` from later partitioned-index benefits
