# Phase 05 Test Plan

## FE Unit Tests

Validate:

- selectivity-driven strategy choice
- expected query shapes route into the planner table
- unsupported forms do not trigger unexpected strategy selection

Primary anchors:

- `fe/fe-core/src/test/java/com/starrocks/planner/VectorIndexTest.java`
- `fe/fe-core/src/test/java/com/starrocks/sql/analyzer/AnalyzeVectorIndexDMLTest.java`

## SQL Tests

Validate:

- EXPLAIN output exposes selected strategy
- forced strategy modes match expected plan text
- broad vs selective queries choose different paths when expected

## BE Tests

Validate:

- planner-provided strategy is honored
- forced strategy runs do not silently route elsewhere

## Regression Risks

- planner thresholds become opaque
- runtime silently overrides planner choice
- optional correlation hook leaks into default behavior

## Exit Conditions

- `A4` can be forced, explained, and benchmarked
- optional correlation-aware hook is documented but not required for acceptance
