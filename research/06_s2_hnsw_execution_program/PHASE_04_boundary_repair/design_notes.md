# Phase 04 Design Notes

## First Repair Mechanism

Recommended first mechanism:

- **cross-partition stitching**

Reason:

- it repairs the exact weakness introduced by partitioning
- it avoids duplicating data immediately
- it is easier to explain in ablation terms than overlap/replication

## Design Intent

The repair layer should answer one narrow question:

> If partitioning hurts recall at cell borders, how much of that can be recovered by explicit cross-partition connectivity?

It should not try to solve:

- planner fallback
- correlation-aware behavior
- adaptive cell selection

## Primary Reference Files

- `be/src/storage/rowset/segment_iterator.cpp`
- future hybrid index modules under `be/src/storage/index/`
- `be/src/storage/index/vector/tenann_index_reader.cpp` as conceptual reference for ANN read behavior

## Candidate Repair Shapes

### R1: Explicit stitched edges

- persist boundary neighbor links between adjacent partitions
- query can traverse or expand candidates across those links

### R2: Overlap / replication

- duplicate boundary vectors into neighboring cells
- simpler query-time behavior
- less clean for ablation because storage duplication changes partition semantics

### R3: Coarse fallback level

- maintain a coarser partition layer for recovery
- potentially strong but less isolated as a first repair slice

## Rejected For First Pass

### Reject overlap as first repair

Reason:

- duplicates data
- complicates index size comparisons
- mixes storage layout change with recovery logic

### Reject coarse fallback as first repair

Reason:

- introduces multi-level planning semantics too early

## Required Outputs

- precise definition of “stitched candidate”
- clear separation between partition-local and repaired search behavior
- measurable cost model inputs for future planner adaptation
