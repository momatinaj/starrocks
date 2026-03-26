# Phase 01 Sprint Plan

## Sprint Goal

Stand up the execution framework for the S2 HNSW program so later implementation phases can proceed with:

- stable baselines
- exact code-touch maps
- reusable benchmark terminology
- explicit feature-switch boundaries for ablation

## Sprint Scope

### In scope

- define execution artifacts and review workflow
- map FE and BE integration surfaces
- define benchmark axes and result recording format
- define baseline and ablation identifiers used across all later phases

### Out of scope

- modifying StarRocks implementation
- introducing new planner logic
- implementing any hybrid index file format

## Stories

### FE stories

- Identify the exact FE rule and option-plumbing surfaces for spatial+vector planning
- Record where later strategy-selection logic will live

### BE stories

- Identify build and read hooks for vector index construction and query execution
- Record where future hybrid index build/read modules will connect to segment lifecycle

### Test stories

- Map existing FE, BE, and SQL test anchors that future phases should extend
- Define minimum regression coverage expectations by phase

### Benchmarking stories

- Lock baseline names and ablation IDs
- Lock benchmark axes: selectivity, geometry shape, boundary intensity, S2 level, correlation regime

## Acceptance Criteria

- `MASTER_PLAN.md`, `ABLATION_MATRIX.md`, and `RISKS_AND_ASSUMPTIONS.md` are complete
- each future phase has a directory and expected document set
- implementation hooks are referenced by concrete StarRocks file paths
- the benchmark contract is stable enough that later phases can compare results consistently

## Sprint Review Checklist

1. Are baseline names unambiguous?
2. Are feature boundaries clear enough for clean ablations?
3. Are the FE/BE hook files concrete and current?
4. Can the next phase start without redefining the benchmark contract?
