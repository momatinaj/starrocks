# Phase 04 Test Plan

## BE Unit Tests

Primary focus:

- stitching metadata is produced correctly
- reader consumes stitched metadata only when repair is enabled
- repaired search does not alter non-boundary behavior unexpectedly

Likely anchors:

- `be/test/storage/index/vector_search_test.cpp`
- new hybrid index unit tests later added beside existing vector tests

## SQL And Integration Tests

Test cases should include:

- queries whose true nearest neighbors straddle a partition border
- same query with repair on and repair off
- broad interior query where repair should not materially change results

## Regression Risks

- repair changes candidate ordering even when no boundary problem exists
- repair inflates query cost too much for interior-heavy workloads
- repair metadata becomes inconsistent with partition metadata

## Exit Conditions

- explicit boundary-heavy test cases exist
- `A2` vs `A3` can be validated with correctness and performance evidence
