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
#include <string>
#include <vector>

#include "common/status.h"

namespace starrocks {

// ========================================================================
// TabletIndex property keys for spatial-partitioned vector indexes.
// These live in TabletIndex::index_properties() and are persisted in the
// JSON blob inside TabletIndexPB.index_properties.
// ========================================================================
struct SpatialIndexPropertyKeys {
    static constexpr const char* kIsSpatialPartitioned = "is_spatial_partitioned";
    static constexpr const char* kS2Level = "s2_level";
    static constexpr const char* kSpatialLatColumnUid = "spatial_lat_column_uid";
    static constexpr const char* kSpatialLngColumnUid = "spatial_lng_column_uid";
    static constexpr const char* kMinPartitionRows = "min_partition_rows";
};

// ========================================================================
// Per-partition metadata — one entry per S2 cell that received rows.
// ========================================================================
struct SpatialPartitionInfo {
    uint64_t cell_id = 0;     // S2 cell ID at the configured level
    uint32_t row_count = 0;   // number of rows assigned to this partition
    uint32_t first_row_id = 0; // segment-local row ID of the first row in this partition
    bool has_hnsw = false;    // true when row_count >= min_partition_rows and HNSW was built

    bool operator==(const SpatialPartitionInfo& o) const {
        return cell_id == o.cell_id && row_count == o.row_count &&
               first_row_id == o.first_row_id && has_hnsw == o.has_hnsw;
    }
};

// ========================================================================
// Manifest for all spatial partitions within a single (segment, index_id).
// Written as a small binary file alongside the per-partition .vi files.
//
// On-disk layout:
//   [4 bytes] magic        = 0x53504154 ("SPAT")
//   [4 bytes] version      = 1
//   [4 bytes] s2_level
//   [4 bytes] spatial_lat_column_uid
//   [4 bytes] spatial_lng_column_uid
//   [4 bytes] min_partition_rows
//   [4 bytes] partition_count
//   For each partition:
//     [8 bytes] cell_id       (uint64, little-endian)
//     [4 bytes] row_count     (uint32)
//     [4 bytes] first_row_id  (uint32)
//     [1 byte]  has_hnsw      (0 or 1)
// ========================================================================
class SpatialPartitionManifest {
public:
    static constexpr uint32_t kMagic = 0x53504154; // "SPAT" in little-endian ASCII
    static constexpr uint32_t kVersion = 1;
    static constexpr uint32_t kDefaultMinPartitionRows = 100;
    static constexpr uint32_t kHeaderSize = 7 * sizeof(uint32_t); // magic + version + 5 fields
    static constexpr uint32_t kPartitionRecordSize = sizeof(uint64_t) + 2 * sizeof(uint32_t) + 1;

    SpatialPartitionManifest() = default;

    SpatialPartitionManifest(int s2_level, int32_t lat_col_uid, int32_t lng_col_uid, uint32_t min_partition_rows)
            : _s2_level(s2_level),
              _spatial_lat_column_uid(lat_col_uid),
              _spatial_lng_column_uid(lng_col_uid),
              _min_partition_rows(min_partition_rows) {}

    // Populate from TabletIndex::index_properties(). Returns error if
    // required keys are missing or values are invalid.
    Status init_from_properties(const std::map<std::string, std::string>& index_props);

    // Export back to a property map suitable for TabletIndex storage.
    void to_properties(std::map<std::string, std::string>* index_props) const;

    // ---- Partition list manipulation ----
    void add_partition(const SpatialPartitionInfo& info);
    const std::vector<SpatialPartitionInfo>& partitions() const { return _partitions; }

    // ---- Accessors ----
    int s2_level() const { return _s2_level; }
    int32_t spatial_lat_column_uid() const { return _spatial_lat_column_uid; }
    int32_t spatial_lng_column_uid() const { return _spatial_lng_column_uid; }
    uint32_t min_partition_rows() const { return _min_partition_rows; }
    uint32_t partition_count() const { return static_cast<uint32_t>(_partitions.size()); }

    // Find partitions whose cell_id is in the given set (query-time routing).
    std::vector<const SpatialPartitionInfo*> find_matching_partitions(
            const std::vector<uint64_t>& query_cell_ids) const;

    // ---- Serialization ----
    // Serialize to a binary buffer.
    Status serialize(std::string* output) const;

    // Deserialize from a binary buffer.
    static StatusOr<SpatialPartitionManifest> deserialize(const std::string& data);

    // Compute the expected serialized size.
    size_t serialized_size() const {
        return kHeaderSize + _partitions.size() * kPartitionRecordSize;
    }

    bool operator==(const SpatialPartitionManifest& o) const {
        return _s2_level == o._s2_level && _spatial_lat_column_uid == o._spatial_lat_column_uid &&
               _spatial_lng_column_uid == o._spatial_lng_column_uid &&
               _min_partition_rows == o._min_partition_rows && _partitions == o._partitions;
    }

private:
    int _s2_level = 0;
    int32_t _spatial_lat_column_uid = -1;
    int32_t _spatial_lng_column_uid = -1;
    uint32_t _min_partition_rows = kDefaultMinPartitionRows;
    std::vector<SpatialPartitionInfo> _partitions;
};

} // namespace starrocks
