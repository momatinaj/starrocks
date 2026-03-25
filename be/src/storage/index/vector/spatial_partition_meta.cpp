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

#include "storage/index/vector/spatial_partition_meta.h"

#include <cstring>

#include "geo/s2_cell_utils.h"

namespace starrocks {

Status SpatialPartitionManifest::init_from_properties(const std::map<std::string, std::string>& index_props) {
    auto find_required = [&](const char* key) -> StatusOr<std::string> {
        auto it = index_props.find(key);
        if (it == index_props.end()) {
            return Status::InvalidArgument(fmt::format("missing required property: {}", key));
        }
        return it->second;
    };

    auto is_partitioned_it = index_props.find(SpatialIndexPropertyKeys::kIsSpatialPartitioned);
    if (is_partitioned_it == index_props.end() || is_partitioned_it->second != "true") {
        return Status::InvalidArgument("index is not spatial-partitioned");
    }

    ASSIGN_OR_RETURN(auto s2_level_str, find_required(SpatialIndexPropertyKeys::kS2Level));
    _s2_level = std::stoi(s2_level_str);
    if (!s2_is_valid_level(_s2_level)) {
        return Status::InvalidArgument(fmt::format("invalid s2_level: {}", _s2_level));
    }

    ASSIGN_OR_RETURN(auto lat_str, find_required(SpatialIndexPropertyKeys::kSpatialLatColumnUid));
    _spatial_lat_column_uid = std::stoi(lat_str);

    ASSIGN_OR_RETURN(auto lng_str, find_required(SpatialIndexPropertyKeys::kSpatialLngColumnUid));
    _spatial_lng_column_uid = std::stoi(lng_str);

    auto min_rows_it = index_props.find(SpatialIndexPropertyKeys::kMinPartitionRows);
    if (min_rows_it != index_props.end()) {
        _min_partition_rows = static_cast<uint32_t>(std::stoul(min_rows_it->second));
    }

    return Status::OK();
}

void SpatialPartitionManifest::to_properties(std::map<std::string, std::string>* index_props) const {
    (*index_props)[SpatialIndexPropertyKeys::kIsSpatialPartitioned] = "true";
    (*index_props)[SpatialIndexPropertyKeys::kS2Level] = std::to_string(_s2_level);
    (*index_props)[SpatialIndexPropertyKeys::kSpatialLatColumnUid] = std::to_string(_spatial_lat_column_uid);
    (*index_props)[SpatialIndexPropertyKeys::kSpatialLngColumnUid] = std::to_string(_spatial_lng_column_uid);
    (*index_props)[SpatialIndexPropertyKeys::kMinPartitionRows] = std::to_string(_min_partition_rows);
}

void SpatialPartitionManifest::add_partition(const SpatialPartitionInfo& info) {
    _partitions.push_back(info);
}

std::vector<const SpatialPartitionInfo*> SpatialPartitionManifest::find_matching_partitions(
        const std::vector<uint64_t>& query_cell_ids) const {
    std::vector<const SpatialPartitionInfo*> matches;
    for (const auto& partition : _partitions) {
        for (uint64_t qid : query_cell_ids) {
            if (partition.cell_id == qid) {
                matches.push_back(&partition);
                break;
            }
        }
    }
    return matches;
}

// ========================================================================
// Binary serialization
// ========================================================================

namespace {

template <typename T>
void write_le(std::string* buf, T value) {
    const char* p = reinterpret_cast<const char*>(&value);
    buf->append(p, sizeof(T));
}

template <typename T>
StatusOr<T> read_le(const char* data, size_t data_size, size_t offset) {
    if (offset + sizeof(T) > data_size) {
        return Status::Corruption("manifest truncated");
    }
    T value;
    std::memcpy(&value, data + offset, sizeof(T));
    return value;
}

} // anonymous namespace

Status SpatialPartitionManifest::serialize(std::string* output) const {
    output->clear();
    output->reserve(serialized_size());

    write_le<uint32_t>(output, kMagic);
    write_le<uint32_t>(output, kVersion);
    write_le<uint32_t>(output, static_cast<uint32_t>(_s2_level));
    write_le<uint32_t>(output, static_cast<uint32_t>(_spatial_lat_column_uid));
    write_le<uint32_t>(output, static_cast<uint32_t>(_spatial_lng_column_uid));
    write_le<uint32_t>(output, _min_partition_rows);
    write_le<uint32_t>(output, static_cast<uint32_t>(_partitions.size()));

    for (const auto& p : _partitions) {
        write_le<uint64_t>(output, p.cell_id);
        write_le<uint32_t>(output, p.row_count);
        write_le<uint32_t>(output, p.first_row_id);
        write_le<uint8_t>(output, p.has_hnsw ? 1 : 0);
    }

    return Status::OK();
}

StatusOr<SpatialPartitionManifest> SpatialPartitionManifest::deserialize(const std::string& data) {
    const char* d = data.data();
    size_t sz = data.size();

    if (sz < kHeaderSize) {
        return Status::Corruption(fmt::format("manifest too small: {} bytes", sz));
    }

    size_t offset = 0;

    ASSIGN_OR_RETURN(auto magic, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);
    if (magic != kMagic) {
        return Status::Corruption(fmt::format("bad manifest magic: 0x{:08X}", magic));
    }

    ASSIGN_OR_RETURN(auto version, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);
    if (version != kVersion) {
        return Status::Corruption(fmt::format("unsupported manifest version: {}", version));
    }

    ASSIGN_OR_RETURN(auto s2_level, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);

    ASSIGN_OR_RETURN(auto lat_uid_raw, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);
    int32_t lat_uid = static_cast<int32_t>(lat_uid_raw);

    ASSIGN_OR_RETURN(auto lng_uid_raw, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);
    int32_t lng_uid = static_cast<int32_t>(lng_uid_raw);

    ASSIGN_OR_RETURN(auto min_rows, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);

    ASSIGN_OR_RETURN(auto partition_count, read_le<uint32_t>(d, sz, offset));
    offset += sizeof(uint32_t);

    size_t expected = kHeaderSize + static_cast<size_t>(partition_count) * kPartitionRecordSize;
    if (sz < expected) {
        return Status::Corruption(fmt::format("manifest truncated: expected {} bytes, got {}", expected, sz));
    }

    SpatialPartitionManifest manifest(static_cast<int>(s2_level), lat_uid, lng_uid, min_rows);

    for (uint32_t i = 0; i < partition_count; i++) {
        SpatialPartitionInfo info;

        ASSIGN_OR_RETURN(info.cell_id, read_le<uint64_t>(d, sz, offset));
        offset += sizeof(uint64_t);

        ASSIGN_OR_RETURN(info.row_count, read_le<uint32_t>(d, sz, offset));
        offset += sizeof(uint32_t);

        ASSIGN_OR_RETURN(info.first_row_id, read_le<uint32_t>(d, sz, offset));
        offset += sizeof(uint32_t);

        ASSIGN_OR_RETURN(auto hnsw_flag, read_le<uint8_t>(d, sz, offset));
        offset += sizeof(uint8_t);
        info.has_hnsw = (hnsw_flag != 0);

        manifest.add_partition(info);
    }

    return manifest;
}

} // namespace starrocks
