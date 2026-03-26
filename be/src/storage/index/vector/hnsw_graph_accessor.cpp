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

#include "storage/index/vector/hnsw_graph_accessor.h"

#include <cstring>
#include <fstream>

#include "common/logging.h"
#include "fs/fs.h"

namespace starrocks {

template <typename T>
Status HNSWGraphAccessor::_read_value(const uint8_t* data, size_t size, size_t& offset, T& out) {
    if (offset + sizeof(T) > size) {
        return Status::Corruption("Unexpected end of file reading value");
    }
    std::memcpy(&out, data + offset, sizeof(T));
    offset += sizeof(T);
    return Status::OK();
}

template <typename T>
Status HNSWGraphAccessor::_read_vector(const uint8_t* data, size_t size, size_t& offset, std::vector<T>& out) {
    uint64_t count = 0;
    RETURN_IF_ERROR(_read_value(data, size, offset, count));
    if (count > (1ULL << 40)) {
        return Status::Corruption("Vector size too large");
    }
    size_t byte_count = count * sizeof(T);
    if (offset + byte_count > size) {
        return Status::Corruption("Unexpected end of file reading vector");
    }
    out.resize(count);
    if (count > 0) {
        std::memcpy(out.data(), data + offset, byte_count);
    }
    offset += byte_count;
    return Status::OK();
}

Status HNSWGraphAccessor::_read_hnsw_graph(const uint8_t* data, size_t size, size_t& offset) {
    RETURN_IF_ERROR(_read_vector(data, size, offset, _assign_probas));
    RETURN_IF_ERROR(_read_vector(data, size, offset, _cum_nneighbor_per_level));
    RETURN_IF_ERROR(_read_vector(data, size, offset, _levels));
    RETURN_IF_ERROR(_read_vector(data, size, offset, _offsets));
    RETURN_IF_ERROR(_read_vector(data, size, offset, _neighbors));
    RETURN_IF_ERROR(_read_value(data, size, offset, _entry_point));
    RETURN_IF_ERROR(_read_value(data, size, offset, _max_level));
    RETURN_IF_ERROR(_read_value(data, size, offset, _ef_construction));
    RETURN_IF_ERROR(_read_value(data, size, offset, _ef_search));

    int upper_beam = 1;
    RETURN_IF_ERROR(_read_value(data, size, offset, upper_beam));

    // Derive M from cum_nneighbor_per_level:
    // Level 0 has 2*M neighbors, level 1+ has M neighbors.
    // cum_nneighbor_per_level[0] = 2*M, cum_nneighbor_per_level[1] = 2*M + M
    if (_cum_nneighbor_per_level.size() >= 2) {
        int level0_neighbors = _cum_nneighbor_per_level[0];
        _M = level0_neighbors / 2;
    } else if (_cum_nneighbor_per_level.size() == 1) {
        _M = _cum_nneighbor_per_level[0] / 2;
    }

    return Status::OK();
}

Status HNSWGraphAccessor::_read_flat_storage(const uint8_t* data, size_t size, size_t& offset) {
    // IndexFlat storage: FourCC + index header + codes vector
    // FourCC for IndexFlat variants
    uint32_t storage_fourcc = 0;
    RETURN_IF_ERROR(_read_value(data, size, offset, storage_fourcc));

    // Skip storage index header (same 33-byte format: d, ntotal, dummy, dummy, is_trained, metric_type)
    if (offset + kIndexHeaderSize > size) {
        return Status::Corruption("File too small for storage header");
    }
    // Read dimension and ntotal from the storage header (they should match the outer header)
    int32_t storage_d = 0;
    int64_t storage_ntotal = 0;
    size_t header_start = offset;
    RETURN_IF_ERROR(_read_value(data, size, offset, storage_d));
    RETURN_IF_ERROR(_read_value(data, size, offset, storage_ntotal));
    // Skip remaining header fields
    offset = header_start + kIndexHeaderSize;

    // Read codes vector (stored as vector<uint8_t> in Faiss)
    std::vector<uint8_t> codes;
    RETURN_IF_ERROR(_read_vector(data, size, offset, codes));

    // Convert codes to float vectors
    size_t expected_bytes = static_cast<size_t>(storage_ntotal) * storage_d * sizeof(float);
    if (codes.size() < expected_bytes) {
        return Status::Corruption("Stored vector data too small");
    }

    _stored_vectors.resize(static_cast<size_t>(storage_ntotal) * storage_d);
    std::memcpy(_stored_vectors.data(), codes.data(), expected_bytes);

    return Status::OK();
}

Status HNSWGraphAccessor::_parse_file(const std::string& path, bool load_vectors) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return Status::NotFound("Cannot open index file: " + path);
    }
    size_t file_size = ifs.tellg();
    ifs.seekg(0);

    std::vector<uint8_t> buffer(file_size);
    ifs.read(reinterpret_cast<char*>(buffer.data()), file_size);
    if (!ifs.good()) {
        return Status::IOError("Failed to read index file: " + path);
    }
    ifs.close();

    const uint8_t* data = buffer.data();
    size_t offset = 0;

    // Read FourCC
    uint32_t fourcc = 0;
    RETURN_IF_ERROR(_read_value(data, file_size, offset, fourcc));

    if (fourcc != kFourCC_IHNf && fourcc != kFourCC_IHNs && fourcc != kFourCC_IHNp) {
        return Status::NotSupported(
                fmt::format("Unsupported Faiss index FourCC: 0x{:08X}. Expected IndexHNSW variant.", fourcc));
    }

    // Read dimension and ntotal from the index header
    size_t header_start = offset;
    int32_t d = 0;
    int64_t ntotal = 0;
    RETURN_IF_ERROR(_read_value(data, file_size, offset, d));
    RETURN_IF_ERROR(_read_value(data, file_size, offset, ntotal));
    _dim = d;
    _ntotal = ntotal;

    // Skip remaining header fields (dummy, dummy, is_trained, metric_type)
    offset = header_start + kIndexHeaderSize;
    if (offset > file_size) {
        return Status::Corruption("File too small for index header");
    }

    // Read HNSW graph
    RETURN_IF_ERROR(_read_hnsw_graph(data, file_size, offset));

    // Validate
    if (_levels.empty()) {
        return Status::Corruption("HNSW graph has no nodes");
    }
    if (_entry_point < 0 || _entry_point >= static_cast<node_id_t>(_levels.size())) {
        return Status::Corruption("Invalid entry point");
    }
    if (_offsets.size() != _levels.size() + 1) {
        return Status::Corruption("Offsets size mismatch");
    }

    // Optionally read the flat vector storage
    if (load_vectors && fourcc == kFourCC_IHNf && offset < file_size) {
        RETURN_IF_ERROR(_read_flat_storage(data, file_size, offset));
    }

    return Status::OK();
}

Status HNSWGraphAccessor::init(const std::string& index_path, bool load_vectors) {
    RETURN_IF_ERROR(_parse_file(index_path, load_vectors));
    _valid = true;
    return Status::OK();
}

int HNSWGraphAccessor::nb_neighbors(int layer) const {
    if (layer >= static_cast<int>(_cum_nneighbor_per_level.size())) {
        return 0;
    }
    if (layer == 0) return _cum_nneighbor_per_level[0];
    return _cum_nneighbor_per_level[layer] - _cum_nneighbor_per_level[layer - 1];
}

void HNSWGraphAccessor::_neighbor_range(node_id_t node_id, int level, size_t& begin, size_t& end) const {
    size_t base = _offsets[node_id];
    // Start offset within the node's neighbor block: cum_nneighbor_per_level gives
    // cumulative count of neighbor slots up to (but excluding) this level.
    size_t level_begin = (level == 0) ? 0 : _cum_nneighbor_per_level[level - 1];
    size_t level_end = _cum_nneighbor_per_level[level];
    begin = base + level_begin;
    end = base + level_end;
}

std::vector<HNSWGraphAccessor::node_id_t> HNSWGraphAccessor::neighbors(node_id_t node_id, int level) const {
    if (node_id < 0 || node_id >= num_nodes()) return {};
    if (level >= _levels[node_id]) return {};
    if (level >= static_cast<int>(_cum_nneighbor_per_level.size())) return {};

    size_t begin, end;
    _neighbor_range(node_id, level, begin, end);

    std::vector<node_id_t> result;
    result.reserve(end - begin);
    for (size_t i = begin; i < end && i < _neighbors.size(); i++) {
        node_id_t neighbor = _neighbors[i];
        if (neighbor >= 0) {
            result.push_back(neighbor);
        }
    }
    return result;
}

int HNSWGraphAccessor::node_level(node_id_t node_id) const {
    if (node_id < 0 || node_id >= num_nodes()) return 0;
    return _levels[node_id];
}

std::vector<HNSWGraphAccessor::node_id_t> HNSWGraphAccessor::expanded_neighbors(node_id_t node_id, int level) const {
    auto one_hop = neighbors(node_id, level);

    std::unordered_set<node_id_t> expanded_set;
    expanded_set.reserve(one_hop.size() * (_M + 1));

    for (auto n : one_hop) {
        expanded_set.insert(n);
        auto two_hop = neighbors(n, level);
        for (auto nn : two_hop) {
            if (nn != node_id) {
                expanded_set.insert(nn);
            }
        }
    }

    return std::vector<node_id_t>(expanded_set.begin(), expanded_set.end());
}

} // namespace starrocks
