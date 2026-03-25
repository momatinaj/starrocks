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

#include "storage/index/vector/spatial_vector_index_writer.h"

#include "fs/fs_util.h"
#include "geo/s2_cell_utils.h"
#include "storage/index/index_descriptor.h"
#include "storage/index/vector/vector_index_writer.h"
#include "storage/tablet_index.h"
#include "util/slice.h"

namespace starrocks {

SpatialVectorIndexWriter::SpatialVectorIndexWriter(std::shared_ptr<TabletIndex> tablet_index, std::string rowset_dir,
                                                   std::string rowset_id, int segment_id, bool is_element_nullable)
        : _tablet_index(std::move(tablet_index)),
          _rowset_dir(std::move(rowset_dir)),
          _rowset_id(std::move(rowset_id)),
          _segment_id(segment_id),
          _is_element_nullable(is_element_nullable) {}

Status SpatialVectorIndexWriter::init() {
    const auto& props = _tablet_index->index_properties();

    SpatialPartitionManifest probe;
    RETURN_IF_ERROR(probe.init_from_properties(props));

    _s2_level = probe.s2_level();
    _spatial_lat_column_uid = probe.spatial_lat_column_uid();
    _spatial_lng_column_uid = probe.spatial_lng_column_uid();
    _min_partition_rows = probe.min_partition_rows();
    _index_id = _tablet_index->index_id();

    return Status::OK();
}

Status SpatialVectorIndexWriter::append(const Column& vector_column, const std::vector<uint64_t>& cell_ids) {
    size_t num_rows = vector_column.size();
    if (cell_ids.size() != num_rows) {
        return Status::InvalidArgument(
                fmt::format("cell_ids size ({}) != vector_column size ({})", cell_ids.size(), num_rows));
    }

    for (size_t i = 0; i < num_rows; i++) {
        auto& buf = _cell_buffers[cell_ids[i]];
        if (buf.vectors == nullptr) {
            buf.vectors = vector_column.clone_empty();
        }
        buf.vectors->append(vector_column, i, 1);
        buf.segment_row_ids.push_back(_next_row_id);
        _next_row_id++;
    }

    return Status::OK();
}

Status SpatialVectorIndexWriter::finish(uint64_t* index_size) {
    if (_next_row_id == 0) {
        return Status::OK();
    }

    SpatialPartitionManifest manifest(_s2_level, _spatial_lat_column_uid, _spatial_lng_column_uid, _min_partition_rows);

    for (auto& [cell_id, buffer] : _cell_buffers) {
        std::string token = s2_cell_id_to_token(cell_id);
        uint32_t row_count = static_cast<uint32_t>(buffer.segment_row_ids.size());
        bool build_hnsw = row_count >= _min_partition_rows;

        if (build_hnsw) {
            std::string vi_path = IndexDescriptor::partitioned_vector_index_file_path(_rowset_dir, _rowset_id,
                                                                                      _segment_id, _index_id, token);

            std::unique_ptr<VectorIndexWriter> writer;
            VectorIndexWriter::create(_tablet_index, vi_path, _is_element_nullable, &writer);
            RETURN_IF_ERROR(writer->init());
            RETURN_IF_ERROR(writer->append(*buffer.vectors));
            RETURN_IF_ERROR(writer->finish(index_size));
        }

        std::string rowids_path = IndexDescriptor::partition_rowid_map_file_path(_rowset_dir, _rowset_id, _segment_id,
                                                                                 _index_id, token);
        RETURN_IF_ERROR(_write_row_id_map(rowids_path, buffer.segment_row_ids));

        uint32_t first_row_id = buffer.segment_row_ids.empty() ? 0 : buffer.segment_row_ids[0];
        manifest.add_partition({cell_id, row_count, first_row_id, build_hnsw});
    }

    std::string manifest_path =
            IndexDescriptor::partition_manifest_file_path(_rowset_dir, _rowset_id, _segment_id, _index_id);
    RETURN_IF_ERROR(_write_manifest(manifest_path, manifest));

    if (index_size) {
        ASSIGN_OR_RETURN(auto mf, fs::new_random_access_file(manifest_path));
        ASSIGN_OR_RETURN(auto mf_size, mf->get_size());
        *index_size += mf_size;
    }

    _cell_buffers.clear();
    return Status::OK();
}

uint64_t SpatialVectorIndexWriter::total_mem_footprint() const {
    uint64_t mem = 0;
    for (const auto& [_, buf] : _cell_buffers) {
        if (buf.vectors) {
            mem += buf.vectors->byte_size();
        }
        mem += buf.segment_row_ids.capacity() * sizeof(uint32_t);
    }
    return mem;
}

Status SpatialVectorIndexWriter::_write_row_id_map(const std::string& path,
                                                    const std::vector<uint32_t>& row_ids) {
    ASSIGN_OR_RETURN(auto file, fs::new_writable_file(path));
    Slice data(reinterpret_cast<const char*>(row_ids.data()), row_ids.size() * sizeof(uint32_t));
    RETURN_IF_ERROR(file->append(data));
    RETURN_IF_ERROR(file->flush(WritableFile::FLUSH_SYNC));
    return file->close();
}

Status SpatialVectorIndexWriter::_write_manifest(const std::string& path,
                                                  const SpatialPartitionManifest& manifest) {
    std::string buf;
    RETURN_IF_ERROR(manifest.serialize(&buf));
    ASSIGN_OR_RETURN(auto file, fs::new_writable_file(path));
    RETURN_IF_ERROR(file->append(Slice(buf)));
    RETURN_IF_ERROR(file->flush(WritableFile::FLUSH_SYNC));
    return file->close();
}

} // namespace starrocks
