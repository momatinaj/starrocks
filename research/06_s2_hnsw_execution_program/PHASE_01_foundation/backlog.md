# Phase 01 Backlog

## Epics

### E1: Execution Program Scaffolding

- Create phase directory structure
- Define program-level root documents
- Standardize per-phase artifact template

### E2: Integration Surface Mapping

- FE planner and thrift surface map
- BE segment build/read surface map
- geo/S2 support map

### E3: Ablation Contract

- finalize baselines
- finalize ablations
- define benchmark output format

## User Stories

### US1

As an implementer, I want a dedicated execution workspace so implementation planning and benchmark iteration can happen without destabilizing the higher-level research notes.

### US2

As a performance evaluator, I want stable baseline names and ablation toggles so I can compare improvements across phases without redefining the experiment.

### US3

As a future contributor, I want exact FE/BE hook files documented so each sprint can immediately target the right StarRocks modules.

## Technical Subtasks

- document FE vector rewrite and scan option propagation surfaces
- document BE segment writer, array column writer, vector reader, and segment iterator surfaces
- document existing vector and SQL test directories
- define benchmark matrix keys and required metadata for result drops

## Dependencies

- existing research docs under `research/03_*`, `research/04_*`, and `research/05_*`
- current StarRocks vector index plumbing

## Blockers

- none for planning

## Exit Artifacts

- program root docs completed
- phase template established
- review checklist and risk register established
