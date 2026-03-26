# Implementation Roadmap

## Phased Delivery Plan

### Phase 0: Foundation (Weeks 1-2)

**Goal**: Establish infrastructure without user-visible changes.

**Tasks**:
1. Add `SPATIAL_VECTOR` to `IndexType` enum in proto and thrift
2. Add `SPATIAL_VECTOR` to grammar in `StarRocks.g4`
3. Add `IndexDef.IndexType.SPATIAL_VECTOR` in FE
4. Add `.hvi` file path support in `IndexDescriptor`
5. Create `be/src/storage/index/hybrid_vector/` directory
6. Implement S2CellIndex data structure (cell ID → offset mapping)
7. Write unit tests for S2CellIndex

**Deliverables**: Infrastructure code, no user-visible features  
**Risk**: Low  
**Dependencies**: None

### Phase 1: Cascade Pipeline (Weeks 3-4)

**Goal**: Deliver basic spatial+vector query optimization with no new index.

**Tasks**:
1. Extend `RewriteToVectorPlanRule` to detect spatial predicates
2. Implement spatial predicate extraction (ST_Contains, ST_DWithin, ST_Within)
3. Pass spatial predicate info in `TVectorSearchOptions`
4. In BE, evaluate spatial predicate to produce row ID filter
5. Pass row ID filter to existing HNSW reader via `del_id_filter`
6. Write FE planner tests for spatial+vector pattern detection
7. Write SQL integration tests

**Deliverables**: Queries with spatial+vector predicates are optimized (cascade execution)  
**Risk**: Low  
**Dependencies**: Phase 0  
**User value**: Immediate — spatial+vector queries work correctly and use vector index

### Phase 2: S2 Cell Statistics (Weeks 5-6)

**Goal**: Enable selectivity estimation for spatial predicates.

**Tasks**:
1. Implement S2CellStatistics collection during segment write
2. Store cell statistics in segment metadata (or side file)
3. Implement `SpatialSelectivityEstimator` in FE
4. Add selectivity-based strategy selection (brute-force vs HNSW+filter vs HNSW+postfilter)
5. Expose selectivity estimate in EXPLAIN output

**Deliverables**: Query planner makes cost-based decisions for spatial+vector queries  
**Risk**: Low-Medium  
**Dependencies**: Phase 1

### Phase 3: Spatial-Partitioned HNSW Index (Weeks 7-12)

**Goal**: Implement the core hybrid index with S2 cell partitioning.

**Tasks**:
1. Implement `HybridVectorIndexBuilder`
   - S2 cell computation from location column
   - Partition vectors by cell
   - Build per-partition HNSW via TenANN
   - Write .hvi file format
2. Implement `HybridVectorIndexReader`
   - Read .hvi file and S2 Cell Index
   - Resolve spatial query to partition set
   - Search matching partitions
   - Merge results across partitions
3. Integrate builder into `SegmentWriter`
4. Integrate reader into `SegmentIterator`
5. Handle `CREATE INDEX ... USING SPATIAL_VECTOR` in FE
6. Handle `ALTER TABLE ADD/DROP INDEX` for hybrid index
7. Handle compaction (rebuild hybrid index during merge)
8. Write comprehensive tests (unit, integration, SQL)

**Deliverables**: Fully functional hybrid spatial+vector index  
**Risk**: Medium-High  
**Dependencies**: Phase 0, Phase 2  
**User value**: Significant — 3-10x speedup for spatial+vector queries vs cascade

### Phase 4: Filtered HNSW with ACORN (Weeks 13-16)

**Goal**: Implement ACORN-style traversal for within-partition search.

**Tasks**:
1. Add S2 cell ID and coordinate metadata to HNSW nodes
2. Implement `S2CellPredicateFilter`
3. Modify TenANN HNSW search for 2-hop exploration on filtered nodes
4. Implement selectivity-based strategy switching (blind 2-hop, directed 2-hop, standard)
5. Integrate with hybrid index reader (search within partitions with filter)
6. Benchmark against Phase 3 for various selectivity ranges

**Deliverables**: ACORN-style filtered search within spatial partitions  
**Risk**: Medium (requires TenANN modification)  
**Dependencies**: Phase 3

### Phase 5: Cross-Partition Stitching (Weeks 17-20)

**Goal**: Address boundary problem between adjacent spatial partitions.

**Tasks**:
1. Implement cross-partition edge computation (StitchedVamana approach)
2. Store stitching edges in .hvi file
3. Modify search to follow cross-partition edges when near boundaries
4. Benchmark recall improvement vs. overhead

**Deliverables**: Improved recall for queries near partition boundaries  
**Risk**: Medium  
**Dependencies**: Phase 3

### Phase 6: Optimization and Documentation (Weeks 21-24)

**Goal**: Production readiness.

**Tasks**:
1. Performance tuning (S2 level selection heuristics, efSearch tuning)
2. Memory optimization (lazy partition loading, block cache integration)
3. Shared-data mode support (index caching on object storage)
4. Cost model calibration with real workloads
5. Documentation:
   - User-facing docs in `docs/en/table_design/indexes/`
   - Configuration documentation
   - Best practices guide
6. Remove `enable_experimental_vector` gate for hybrid index (or add new gate)

**Deliverables**: Production-ready hybrid index  
**Risk**: Low  
**Dependencies**: Phase 3-5

## Timeline Summary

```
Week  1  2  3  4  5  6  7  8  9  10 11 12 13 14 15 16 17 18 19 20 21 22 23 24
      ├──P0──┤
               ├──P1──┤
                        ├─P2─┤
                              ├────────P3────────┤
                                                   ├────P4────┤
                                                               ├───P5───┤
                                                                         ├──P6──┤
```

## Milestones

| Milestone | Week | Description |
|-----------|------|-------------|
| M0 | 2 | Infrastructure in place |
| M1 | 4 | Cascade pipeline working (first user-visible value) |
| M2 | 6 | Selectivity-based strategy selection |
| M3 | 12 | Hybrid index MVP (core feature complete) |
| M4 | 16 | ACORN traversal integrated |
| M5 | 20 | Cross-partition stitching |
| M6 | 24 | Production-ready release |

## Success Criteria

| Metric | Target |
|--------|--------|
| Recall@10 | ≥ 0.95 for all selectivity ranges |
| Latency improvement | ≥ 3x over cascade for 0.1-10% selectivity |
| QPS | ≥ 1000 for 10K vectors/segment |
| Build overhead | < 2x over standalone HNSW |
| Memory overhead | < 1.5x over standalone HNSW |
