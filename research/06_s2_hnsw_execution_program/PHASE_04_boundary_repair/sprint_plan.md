# Phase 04 Sprint Plan

## Sprint Goal

Define and isolate the first boundary-repair mechanism for the S2 partitioned HNSW index so recall loss at cell borders can be measured separately from partitioning.

## Sprint Scope

### In scope

- define boundary-failure benchmark cases
- choose first repair mechanism
- specify build-time and query-time impact
- preserve on/off toggling for ablation

### Out of scope

- planner adaptation
- correlation-aware tuning as a default behavior

## Stories

### BE stories

- define where repair metadata is produced during build
- define where repaired traversal or repaired candidate expansion occurs during read
- define how repair can be disabled cleanly

### Test stories

- create explicit boundary-heavy correctness cases
- ensure interior-heavy behavior remains stable when repair is enabled

### Benchmarking stories

- compare `A2` and `A3`
- quantify recall gains, build cost, and memory increase

## Acceptance Criteria

- repair mechanism is specified independently of partitioning
- `A3` can be benchmarked directly against `A2`
- performance/recall tradeoffs are explicit in the phase plan
