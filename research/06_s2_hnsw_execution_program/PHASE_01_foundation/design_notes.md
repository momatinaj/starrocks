# Phase 01 Design Notes

## Design Decision

The execution program is separated from the conceptual research notes so later turns can iterate aggressively on:

- implementation details
- benchmark contracts
- sprint-level planning
- result collection

without rewriting the research rationale each time.

## Reused Research Inputs

- `research/03_algorithmic_design/spatial_partitioned_hnsw.md`
- `research/03_algorithmic_design/cost_model.md`
- `research/03_algorithmic_design/spatial_vector_correlation.md`
- `research/04_starrocks_design/architecture_fit.md`
- `research/04_starrocks_design/segment_integration.md`

## Initial Architecture Slices

### Slice A: FE planner and option propagation

- `RewriteToVectorPlanRule.java`
- `VectorSearchOptions.java`
- `OlapScanNode.java`
- `PlanNodes.thrift`

### Slice B: BE build path

- `segment_writer.cpp`
- `array_column_writer.cpp`
- `vector_index_writer.cpp`

### Slice C: BE query path

- `olap_chunk_source.cpp`
- `segment_iterator.cpp`
- `tenann_index_reader.cpp`

### Slice D: Spatial support

- `geo_types.h`
- `geo_functions.cpp`

### Slice E: Validation

- FE unit tests
- BE unit tests
- SQL integration tests
- BE microbenchmarks

## Rejected Alternatives

### Rejected: mix execution docs into `research/04_starrocks_design/`

Reason:

- that section should stay as the architecture rationale
- it is not a sprint workspace

### Rejected: define ablations inside each phase only

Reason:

- later phases would drift in terminology
- benchmark results would become hard to compare

## Phase Output Contract

Every later phase must answer:

1. What exact optimization is introduced now?
2. What baseline and prior ablation is it compared against?
3. What concrete FE/BE files are likely touched?
4. What tests prove correctness?
5. What benchmark cases prove the optimization matters?
