# S2 HNSW Execution Program

This directory is the operational workspace for implementing and validating an **S2 spatial-partitioned HNSW index with boundary repair and query planner fallback** in StarRocks.

It is intentionally separate from the higher-level research notes under `research/01_*` through `research/05_*`.

## Purpose

This program exists to make the work:

- implementation-oriented
- phaseable in agile sprints
- benchmarkable against stable baselines
- ablation-friendly, so every optimization can be measured independently

## Relationship To Existing Research

This execution program is grounded in:

- `research/03_algorithmic_design/spatial_partitioned_hnsw.md`
- `research/03_algorithmic_design/cost_model.md`
- `research/03_algorithmic_design/spatial_vector_correlation.md`
- `research/04_starrocks_design/architecture_fit.md`
- `research/04_starrocks_design/segment_integration.md`

Those files explain **why** the design is promising. This directory defines **how** to implement and validate it.

## Program Artifacts

- `MASTER_PLAN.md`
  High-level program map, phase sequencing, architecture slices, and delivery principles.

- `ABLATION_MATRIX.md`
  Contract for baselines, feature toggles, datasets, metrics, and expected comparisons.

- `RISKS_AND_ASSUMPTIONS.md`
  Program-level risks, assumptions, and decision checkpoints.

- `PHASE_01_foundation/` through `PHASE_06_full_package_validation/`
  Detailed agile planning packs for each phase.

- `results/`
  Reserved for later benchmark outputs, regression notes, and performance tables.

## Phase Summary

1. **Foundation and Ablation Harness**
   Define execution scaffolding, benchmark contract, and feature-switch boundaries.
2. **Baseline Fallback**
   Implement a correct spatial+vector fallback path without a new hybrid index.
3. **Partitioned Index MVP**
   Build the first S2-partitioned HNSW index.
4. **Boundary Repair**
   Add cross-partition recovery and measure recall gains separately.
5. **Planner Adaptation**
   Make the FE choose among fallback, partitioned, and repaired modes using selectivity.
6. **Full Validation**
   Run correctness, regression, SQL, and benchmark validation for the full package.

## Ablation Principle

Every optimization must remain independently switchable:

- partitioning
- boundary repair
- planner fallback
- optional correlation-aware tuning

If two optimizations cannot be independently toggled, the ablation study becomes ambiguous.

## Expected Future Workflow

1. Use the current phase directory as the active sprint workspace.
2. Keep implementation notes and benchmark expectations current there.
3. Record measured results under `results/`.
4. Promote only validated conclusions back into the higher-level research documents.
