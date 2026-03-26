# Phase 04 Backlog

## Epics

### E1: Boundary Failure Characterization

- define what counts as a boundary-heavy query
- identify metrics for boundary recall degradation

### E2: Repair Mechanism Design

- specify first repair mechanism
- define metadata generation and read-path consumption
- define toggling model

### E3: Cost Accounting

- capture additional index size
- capture build overhead
- capture query overhead

## User Stories

### US1

As a search engineer, I want border cases isolated so I can measure whether partition boundaries are the real source of recall loss.

### US2

As a benchmark owner, I want the first repair layer to be cleanly switchable so `A2` and `A3` remain comparable.

### US3

As a future planner developer, I want repair behavior well-defined before planner adaptation starts routing into it.

## Technical Subtasks

- define a boundary-nearness metric for points and queries
- define stitching metadata schema
- define query-time use of stitched connections or stitched candidate expansion
- define acceptance thresholds for repair value

## Recommended First Repair

Use **cross-partition stitching** first because:

- it maps directly to the research recommendation
- it preserves partition structure
- it creates a cleaner ablation than overlap/replication

## Alternative Mechanisms To Note

- overlap / replication
- coarse-level fallback partition

## Dependencies

- Phase 03 partitioned MVP

## Exit Artifacts

- repair design fixed
- boundary benchmark contract fixed
- `A2` vs `A3` comparison plan complete
