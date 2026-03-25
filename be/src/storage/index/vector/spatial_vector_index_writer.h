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
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "column/column.h"
#include "common/statusor.h"
#include "storage/index/vector/spatial_partition_meta.h"

namespace starrocks {

class TabletIndex;

// Writes a spatially-partitioned vector index: one HNSW per qualifying S2 cell.
//
// Usage:
//   1. init()
//   2. append(vector_column, cell_ids)  — may be called multiple times
//   3. finish(&index_size)              — builds per-cell indexes, writes manifest
//
// The caller is responsible for computing S2 cell IDs (via s2_cell_id_from_latlng)
// and passing them alongside the vector column data.
class SpatialVectorIndexWriter {
public:
    SpatialVectorIndexWriter(std::shared_ptr<TabletIndex> tablet_index, std::string rowset_dir,
                             std::string rowset_id, int segment_id, bool is_element_nullable);

    Status init();

    // Append vectors with pre-computed S2 cell IDs.
    // cell_ids.size() must equal vector_column.size().
    Status append(const Column& vector_column, const std::vector<uint64_t>& cell_ids);

    // Build per-partition HNSW indexes, write row-ID maps and manifest.
    Status finish(uint64_t* index_size);

    size_t total_rows() const { return _next_row_id; }
    size_t num_partitions() const { return _cell_buffers.size(); }
    uint64_t total_mem_footprint() const;

private:
    struct CellBuffer {
        ColumnPtr vectors;                       // buffered ArrayColumn for this cell
        std::vector<uint32_t> segment_row_ids;   // segment-local row IDs in append order
    };

    Status _write_row_id_map(const std::string& path, const std::vector<uint32_t>& row_ids);
    Status _write_manifest(const std::string& path, const SpatialPartitionManifest& manifest);

    std::shared_ptr<TabletIndex> _tablet_index;
    std::string _rowset_dir;
    std::string _rowset_id;
    int _segment_id;
    int64_t _index_id = -1;
    bool _is_element_nullable;

    int _s2_level = 0;
    int32_t _spatial_lat_column_uid = -1;
    int32_t _spatial_lng_column_uid = -1;
    uint32_t _min_partition_rows = SpatialPartitionManifest::kDefaultMinPartitionRows;

    std::unordered_map<uint64_t, CellBuffer> _cell_buffers;
    uint32_t _next_row_id = 0;
};

} // namespace starrocks
