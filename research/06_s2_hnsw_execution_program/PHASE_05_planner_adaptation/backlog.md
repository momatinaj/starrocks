# Phase 05 Backlog

## Epics

### E1: Planner Strategy Model

- define selectivity thresholds
- define legal strategies
- define strategy-forcing/debug modes for testing

### E2: FE Surface Integration

- rule placement
- option propagation
- EXPLAIN/debug reporting

### E3: Optional Correlation Hook

- define optional `ρ̂` or user hint input
- keep disabled by default

## User Stories

### US1

As a planner developer, I want strategy choice based on spatial selectivity so broad queries do not pay partitioning cost unnecessarily and tiny queries can fall back to exact distance.

### US2

As a benchmark owner, I want the planner logic isolated from the partitioned index mechanics so I can compare `A3` and `A4` cleanly.

### US3

As a future researcher, I want a place to wire in optional correlation-aware hints without redefining the baseline planner logic.

## Technical Subtasks

- define `σ_s` estimation inputs available to FE
- define strategy enum and explain string contract
- define force-strategy test mode for reproducible comparisons
- define optional correlation hint interface guarded behind an experimental switch

## Dependencies

- Phase 02 fallback baseline
- Phase 03 partitioned MVP
- Phase 04 repair behavior and cost

## Blockers To Watch

- FE thresholds may depend on data not currently surfaced cleanly
- too much runtime heuristic behavior in BE can undermine planner visibility

## Exit Artifacts

- planner strategy table
- explain/debug plan
- optional correlation hook design
