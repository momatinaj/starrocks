# Phase 05 Sprint Plan

## Sprint Goal

Define how the FE planner chooses among brute-force, fallback baseline, partitioned search, and repaired-partition search using spatial selectivity, while keeping correlation-aware tuning optional.

## Sprint Scope

### In scope

- planner decision table using spatial selectivity
- FE option propagation for chosen strategy
- EXPLAIN/debug visibility for strategy selection
- optional guarded path for future correlation-aware hints

### Out of scope

- changing the definition of any earlier baseline or ablation
- making correlation-aware tuning mandatory

## Stories

### FE stories

- decide whether to extend the existing vector rewrite rule or define a new hybrid rule
- encode selected execution strategy in planner/runtime options
- surface decision reasoning in EXPLAIN

### BE stories

- respect planner-selected strategy cleanly
- avoid hidden runtime heuristics that would blur ablations

### Test stories

- FE plan tests for strategy choice
- SQL EXPLAIN tests
- controlled runs forcing each strategy for comparison

### Benchmarking stories

- compare `A3` vs `A4`
- ensure planner helps on mixed workloads rather than only on one query class

## Acceptance Criteria

- planner strategy table is explicit
- planner-selected behavior is observable
- correlation-aware logic remains optional and isolated
