// Copyright 2021-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/statusor.h"
#include "storage/index/vector/spatial_partition_meta.h"

#ifdef WITH_TENANN
#include <tenann/common/seq_view.h>
#endif

namespace starrocks {

class TabletIndex;
class VectorIndexReader;

// Reads a spatially-partitioned vector index: opens per-partition .vi files,
// searches matching partitions, translates row IDs, and merges top-k results.
class SpatialVectorIndexReader {
public:
    struct PartitionState {
        SpatialPartitionInfo info;
        std::vector<uint32_t> segment_row_ids; // loaded from .vi_rowids
        std::shared_ptr<VectorIndexReader> reader; // null if !has_hnsw or init failed
    };

    // Load manifest, row-ID maps, and prepare partition readers.
    Status init(const std::string& rowset_dir, const std::string& rowset_id, int segment_id, int64_t index_id,
                const std::shared_ptr<TabletIndex>& tablet_index,
                const std::map<std::string, std::string>& query_params);

    bool is_valid() const { return _manifest.partition_count() > 0; }
    const SpatialPartitionManifest& manifest() const { return _manifest; }
    const std::vector<PartitionState>& partitions() const { return _partitions; }

    // Translate partition-local row ID to segment-local row ID.
    // Returns -1 if local_id is out of range.
    int64_t translate_row_id(size_t partition_idx, int64_t local_id) const;

    size_t active_reader_count() const;

    // G1: Per-partition oversampling. Each partition searches for k * factor
    // instead of k, then the global merge picks the true top-k.
    void set_oversample_factor(float factor) { _oversample_factor = std::max(1.0f, factor); }
    float oversample_factor() const { return _oversample_factor; }

    // G2: Neighbor cell expansion. When enabled, query_cell_ids are expanded
    // to include S2 edge-neighbor cells before partition matching.
    void set_expand_neighbors(bool expand) { _expand_neighbors = expand; }
    bool expand_neighbors() const { return _expand_neighbors; }

    // G3: Small-cell brute-force. When enabled, matched partitions without
    // HNSW contribute their row IDs as unranked candidates instead of being
    // skipped entirely.
    void set_scan_small_cells(bool scan) { _scan_small_cells = scan; }
    bool scan_small_cells() const { return _scan_small_cells; }

    // G4: Max cover cells for S2 covering. Higher values improve precision
    // for large spatial predicates.
    void set_max_cover_cells(int max_cells) { _max_cover_cells = std::max(1, max_cells); }
    int max_cover_cells() const { return _max_cover_cells; }

#ifdef WITH_TENANN
    // Search partitions matching query_cell_ids. Returns segment-local row IDs
    // and distances, merged across partitions into global top-k.
    Status search(const std::vector<uint64_t>& query_cell_ids, tenann::PrimitiveSeqView query_view, int64_t k,
                  std::vector<int64_t>* result_ids, std::vector<float>* result_distances);
#endif

    // ---- TenANN-free merge utility (testable without HNSW runtime) ----

    struct PartitionResult {
        std::vector<int64_t> local_ids;
        std::vector<float> distances;
        const std::vector<uint32_t>* row_id_map; // partition-local → segment-local
    };

    // Merge results from multiple partitions into global top-k (ascending distance).
    // Translates partition-local IDs to segment-local IDs via row_id_map.
    static void merge_partition_results(const std::vector<PartitionResult>& results, int64_t k,
                                        std::vector<int64_t>* merged_ids, std::vector<float>* merged_distances);

private:
    Status _load_row_id_map(const std::string& path, uint32_t expected_count, std::vector<uint32_t>* row_ids);

    SpatialPartitionManifest _manifest;
    std::vector<PartitionState> _partitions;
    std::string _rowset_dir;
    std::string _rowset_id;
    int _segment_id = 0;
    int64_t _index_id = 0;
    std::shared_ptr<TabletIndex> _tablet_index;
    std::map<std::string, std::string> _query_params;

    float _oversample_factor = 5.0f;
    bool _expand_neighbors = false;
    bool _scan_small_cells = false;
    int _max_cover_cells = 500;
};

} // namespace starrocks
