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

#include "storage/index/vector/spatial_vector_index_reader.h"

#include <algorithm>
#include <queue>

#include "fs/fs_util.h"
#include "geo/s2_cell_utils.h"
#include "storage/index/index_descriptor.h"
#include "storage/index/vector/vector_index_reader.h"
#include "storage/index/vector/vector_index_reader_factory.h"
#include "storage/tablet_index.h"
#include "util/slice.h"

#ifdef WITH_TENANN
#include "storage/index/vector/tenann/tenann_index_utils.h"
#endif

namespace starrocks {

Status SpatialVectorIndexReader::init(const std::string& rowset_dir, const std::string& rowset_id, int segment_id,
                                      int64_t index_id, const std::shared_ptr<TabletIndex>& tablet_index,
                                      const std::map<std::string, std::string>& query_params) {
    _rowset_dir = rowset_dir;
    _rowset_id = rowset_id;
    _segment_id = segment_id;
    _index_id = index_id;
    _tablet_index = tablet_index;
    _query_params = query_params;

    std::string manifest_path =
            IndexDescriptor::partition_manifest_file_path(rowset_dir, rowset_id, segment_id, index_id);

    if (!fs::path_exist(manifest_path)) {
        return Status::OK();
    }

    ASSIGN_OR_RETURN(auto mf_file, fs::new_random_access_file(manifest_path));
    ASSIGN_OR_RETURN(auto mf_size, mf_file->get_size());
    std::string mf_buf(mf_size, '\0');
    RETURN_IF_ERROR(mf_file->read_fully(mf_buf.data(), mf_size));

    ASSIGN_OR_RETURN(_manifest, SpatialPartitionManifest::deserialize(mf_buf));

    _partitions.resize(_manifest.partition_count());
    for (uint32_t i = 0; i < _manifest.partition_count(); i++) {
        auto& state = _partitions[i];
        state.info = _manifest.partitions()[i];

        std::string token = s2_cell_id_to_token(state.info.cell_id);
        std::string rowids_path =
                IndexDescriptor::partition_rowid_map_file_path(rowset_dir, rowset_id, segment_id, index_id, token);
        RETURN_IF_ERROR(_load_row_id_map(rowids_path, state.info.row_count, &state.segment_row_ids));

#ifdef WITH_TENANN
        if (state.info.has_hnsw) {
            std::string vi_path = IndexDescriptor::partitioned_vector_index_file_path(rowset_dir, rowset_id, segment_id,
                                                                                      index_id, token);
            if (fs::path_exist(vi_path)) {
                ASSIGN_OR_RETURN(auto meta, get_vector_meta(tablet_index, query_params));
                auto index_meta = std::make_shared<tenann::IndexMeta>(std::move(meta));
                RETURN_IF_ERROR(
                        VectorIndexReaderFactory::create_from_file(vi_path, index_meta, &state.reader));
                auto status = state.reader->init_searcher(*index_meta, vi_path);
                if (status.is_not_supported()) {
                    state.reader.reset();
                } else if (!status.ok()) {
                    return status;
                }
            }
        }
#endif
    }

    return Status::OK();
}

int64_t SpatialVectorIndexReader::translate_row_id(size_t partition_idx, int64_t local_id) const {
    if (partition_idx >= _partitions.size()) return -1;
    const auto& row_ids = _partitions[partition_idx].segment_row_ids;
    if (local_id < 0 || static_cast<size_t>(local_id) >= row_ids.size()) return -1;
    return static_cast<int64_t>(row_ids[static_cast<size_t>(local_id)]);
}

size_t SpatialVectorIndexReader::active_reader_count() const {
    size_t count = 0;
    for (const auto& p : _partitions) {
        if (p.reader) count++;
    }
    return count;
}

#ifdef WITH_TENANN
Status SpatialVectorIndexReader::search(const std::vector<uint64_t>& query_cell_ids,
                                        tenann::PrimitiveSeqView query_view, int64_t k,
                                        std::vector<int64_t>* result_ids, std::vector<float>* result_distances) {
    auto matching = _manifest.find_matching_partitions(query_cell_ids);
    if (matching.empty()) {
        return Status::OK();
    }

    std::vector<PartitionResult> partition_results;
    partition_results.reserve(matching.size());

    for (const auto* part_info : matching) {
        size_t idx = 0;
        for (size_t i = 0; i < _partitions.size(); i++) {
            if (_partitions[i].info.cell_id == part_info->cell_id) {
                idx = i;
                break;
            }
        }

        auto& state = _partitions[idx];
        if (!state.reader) continue;

        std::vector<int64_t> local_ids(k, -1);
        std::vector<float> local_distances(k, std::numeric_limits<float>::max());

        auto st = state.reader->search(query_view, static_cast<int>(k), local_ids.data(),
                                       reinterpret_cast<uint8_t*>(local_distances.data()), nullptr);
        if (!st.ok()) {
            LOG(WARNING) << "Spatial partition search failed for cell " << part_info->cell_id << ": " << st.message();
            continue;
        }

        PartitionResult pr;
        pr.local_ids = std::move(local_ids);
        pr.distances = std::move(local_distances);
        pr.row_id_map = &state.segment_row_ids;
        partition_results.push_back(std::move(pr));
    }

    merge_partition_results(partition_results, k, result_ids, result_distances);
    return Status::OK();
}
#endif

void SpatialVectorIndexReader::merge_partition_results(const std::vector<PartitionResult>& results, int64_t k,
                                                        std::vector<int64_t>* merged_ids,
                                                        std::vector<float>* merged_distances) {
    // Collect all valid (segment_row_id, distance) pairs
    using IdDist = std::pair<float, int64_t>; // (distance, segment_row_id)
    std::vector<IdDist> all_candidates;

    for (const auto& pr : results) {
        for (size_t i = 0; i < pr.local_ids.size(); i++) {
            int64_t local_id = pr.local_ids[i];
            if (local_id < 0) continue;

            int64_t seg_id = -1;
            if (pr.row_id_map && static_cast<size_t>(local_id) < pr.row_id_map->size()) {
                seg_id = static_cast<int64_t>((*pr.row_id_map)[static_cast<size_t>(local_id)]);
            }
            if (seg_id < 0) continue;

            all_candidates.emplace_back(pr.distances[i], seg_id);
        }
    }

    // Partial sort to get top-k by ascending distance
    if (static_cast<int64_t>(all_candidates.size()) > k) {
        std::partial_sort(all_candidates.begin(), all_candidates.begin() + k, all_candidates.end());
        all_candidates.resize(static_cast<size_t>(k));
    } else {
        std::sort(all_candidates.begin(), all_candidates.end());
    }

    merged_ids->clear();
    merged_distances->clear();
    merged_ids->reserve(all_candidates.size());
    merged_distances->reserve(all_candidates.size());
    for (const auto& [dist, id] : all_candidates) {
        merged_ids->push_back(id);
        merged_distances->push_back(dist);
    }
}

Status SpatialVectorIndexReader::_load_row_id_map(const std::string& path, uint32_t expected_count,
                                                   std::vector<uint32_t>* row_ids) {
    if (!fs::path_exist(path)) {
        return Status::NotFound(fmt::format("row id map not found: {}", path));
    }

    ASSIGN_OR_RETURN(auto file, fs::new_random_access_file(path));
    ASSIGN_OR_RETURN(auto size, file->get_size());

    uint32_t actual_count = static_cast<uint32_t>(size / sizeof(uint32_t));
    if (actual_count != expected_count) {
        return Status::Corruption(
                fmt::format("row id map size mismatch: expected {} entries, got {}", expected_count, actual_count));
    }

    row_ids->resize(actual_count);
    RETURN_IF_ERROR(file->read_fully(row_ids->data(), size));
    return Status::OK();
}

} // namespace starrocks
