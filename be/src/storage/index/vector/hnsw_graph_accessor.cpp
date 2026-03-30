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
#include "fmt/format.h"

namespace starrocks {

namespace {

// Faiss fourcc constants (little-endian encoding of 4-character tags).
// See faiss/impl/index_write.cpp for authoritative definitions.
constexpr uint32_t make_fourcc(char a, char b, char c, char d) {
    return static_cast<uint32_t>(static_cast<unsigned char>(a)) |
           (static_cast<uint32_t>(static_cast<unsigned char>(b)) << 8) |
           (static_cast<uint32_t>(static_cast<unsigned char>(c)) << 16) |
           (static_cast<uint32_t>(static_cast<unsigned char>(d)) << 24);
}

constexpr uint32_t kFourCC_IxMp = make_fourcc('I', 'x', 'M', 'p'); // IndexIDMap
constexpr uint32_t kFourCC_IxM2 = make_fourcc('I', 'x', 'M', '2'); // IndexIDMap2
constexpr uint32_t kFourCC_IHNf = make_fourcc('I', 'H', 'N', 'f'); // IndexHNSWFlat
constexpr uint32_t kFourCC_IHNp = make_fourcc('I', 'H', 'N', 'p'); // IndexHNSWPQ
constexpr uint32_t kFourCC_IHNs = make_fourcc('I', 'H', 'N', 's'); // IndexHNSWSQ
constexpr uint32_t kFourCC_IHN2 = make_fourcc('I', 'H', 'N', '2'); // IndexHNSW2Level
constexpr uint32_t kFourCC_IxPT = make_fourcc('I', 'x', 'P', 'T'); // IndexPreTransform
constexpr uint32_t kFourCC_IxF2 = make_fourcc('I', 'x', 'F', '2'); // IndexFlat L2
constexpr uint32_t kFourCC_IxFI = make_fourcc('I', 'x', 'F', 'I'); // IndexFlat IP
constexpr uint32_t kFourCC_IxFl = make_fourcc('I', 'x', 'F', 'l'); // IndexFlat other

bool is_hnsw_fourcc(uint32_t fourcc) {
    return fourcc == kFourCC_IHNf || fourcc == kFourCC_IHNp || fourcc == kFourCC_IHNs || fourcc == kFourCC_IHN2;
}

bool is_flat_fourcc(uint32_t fourcc) {
    return fourcc == kFourCC_IxF2 || fourcc == kFourCC_IxFI || fourcc == kFourCC_IxFl;
}

std::string fourcc_to_string(uint32_t fc) {
    char buf[5];
    buf[0] = static_cast<char>(fc & 0xFF);
    buf[1] = static_cast<char>((fc >> 8) & 0xFF);
    buf[2] = static_cast<char>((fc >> 16) & 0xFF);
    buf[3] = static_cast<char>((fc >> 24) & 0xFF);
    buf[4] = '\0';
    return std::string(buf);
}

// Matches the layout written by Faiss's write_index_header():
//   int d, idx_t ntotal, idx_t dummy, idx_t dummy,
//   bool is_trained, MetricType metric_type, [float metric_arg if metric_type>1]
struct FaissIndexHeader {
    int d = 0;
    int64_t ntotal = 0;
    int metric_type = 0;
};

template <typename T>
bool read_val(std::ifstream& f, T& val) {
    return static_cast<bool>(f.read(reinterpret_cast<char*>(&val), sizeof(val)));
}

template <typename T>
bool read_vector(std::ifstream& f, std::vector<T>& vec) {
    size_t count;
    if (!read_val(f, count)) return false;
    if (count > static_cast<size_t>(1) << 40) return false; // sanity limit (~1TB)
    vec.resize(count);
    if (count > 0) {
        return static_cast<bool>(f.read(reinterpret_cast<char*>(vec.data()), count * sizeof(T)));
    }
    return true;
}

bool skip_bytes(std::ifstream& f, size_t n) {
    f.seekg(static_cast<std::streamoff>(n), std::ios::cur);
    return f.good();
}

// Reads and discards the contents of a WRITEVECTOR(vec) block,
// where each element is elem_size bytes.
bool skip_vector(std::ifstream& f, size_t elem_size) {
    size_t count;
    if (!read_val(f, count)) return false;
    return skip_bytes(f, count * elem_size);
}

bool read_index_header(std::ifstream& f, FaissIndexHeader& hdr) {
    if (!read_val(f, hdr.d)) return false;
    if (!read_val(f, hdr.ntotal)) return false;
    int64_t dummy;
    if (!read_val(f, dummy)) return false; // dummy1
    if (!read_val(f, dummy)) return false; // dummy2
    bool is_trained;
    if (!read_val(f, is_trained)) return false;
    if (!read_val(f, hdr.metric_type)) return false;
    if (hdr.metric_type > 1) {
        float metric_arg;
        if (!read_val(f, metric_arg)) return false;
    }
    return true;
}

} // anonymous namespace

Status HNSWGraphAccessor::init(const std::string& index_path, bool load_vectors) {
    std::ifstream file(index_path, std::ios::binary);
    if (!file) {
        return Status::IOError("Cannot open index file: " + index_path);
    }

    // --- Phase 1: Read top-level fourcc and handle wrapping layers ---
    uint32_t top_fourcc;
    if (!read_val(file, top_fourcc)) {
        return Status::Corruption("Cannot read fourcc from: " + index_path);
    }

    bool has_idmap = false;

    // IndexIDMap / IndexIDMap2 wrapping
    if (top_fourcc == kFourCC_IxMp || top_fourcc == kFourCC_IxM2) {
        has_idmap = true;
        FaissIndexHeader idmap_hdr;
        if (!read_index_header(file, idmap_hdr)) {
            return Status::Corruption("Cannot read IndexIDMap header from: " + index_path);
        }
        // Next: the inner index (fourcc + data)
        if (!read_val(file, top_fourcc)) {
            return Status::Corruption("Cannot read inner index fourcc from: " + index_path);
        }
    }

    // IndexPreTransform wrapping (not supported — would need to parse VectorTransform chain)
    if (top_fourcc == kFourCC_IxPT) {
        return Status::NotSupported(
                "IndexPreTransform (cosine/IP metric with L2Norm) is not supported by HNSWGraphAccessor. "
                "Rebuild the vector index with metric_type=l2_distance.");
    }

    // Now we should have an IndexHNSW fourcc
    if (!is_hnsw_fourcc(top_fourcc)) {
        return Status::NotSupported(fmt::format("Unsupported Faiss index type '{}' (fourcc=0x{:08X}) in: {}",
                                                fourcc_to_string(top_fourcc), top_fourcc, index_path));
    }

    // --- Phase 2: Read IndexHNSW header ---
    FaissIndexHeader hnsw_hdr;
    if (!read_index_header(file, hnsw_hdr)) {
        return Status::Corruption("Cannot read IndexHNSW header from: " + index_path);
    }
    _dim = hnsw_hdr.d;
    _ntotal = hnsw_hdr.ntotal;

    // --- Phase 3: Read HNSW graph data ---
    // Format matches faiss::write_HNSW():
    //   WRITEVECTOR(assign_probas)            double[]
    //   WRITEVECTOR(cum_nneighbor_per_level)  int[]
    //   WRITEVECTOR(levels)                   int[]
    //   WRITEVECTOR(offsets)                  size_t[]
    //   WRITEVECTOR(neighbors)               storage_idx_t[] (int32_t)
    //   WRITE1(entry_point)                  storage_idx_t (int32_t)
    //   WRITE1(max_level)                    int
    //   WRITE1(efConstruction)               int
    //   WRITE1(efSearch)                     int
    //   WRITE1(upper_beam)                   int (deprecated, always 1)

    if (!read_vector(file, _assign_probas)) {
        return Status::Corruption("Failed to read assign_probas from: " + index_path);
    }
    if (!read_vector(file, _cum_nneighbor_per_level)) {
        return Status::Corruption("Failed to read cum_nneighbor_per_level from: " + index_path);
    }
    if (!read_vector(file, _levels)) {
        return Status::Corruption("Failed to read levels from: " + index_path);
    }
    if (!read_vector(file, _offsets)) {
        return Status::Corruption("Failed to read offsets from: " + index_path);
    }
    if (!read_vector(file, _neighbors)) {
        return Status::Corruption("Failed to read neighbors from: " + index_path);
    }
    if (!read_val(file, _entry_point)) {
        return Status::Corruption("Failed to read entry_point from: " + index_path);
    }
    if (!read_val(file, _max_level)) {
        return Status::Corruption("Failed to read max_level from: " + index_path);
    }
    if (!read_val(file, _ef_construction)) {
        return Status::Corruption("Failed to read efConstruction from: " + index_path);
    }
    if (!read_val(file, _ef_search)) {
        return Status::Corruption("Failed to read efSearch from: " + index_path);
    }
    int upper_beam_deprecated;
    if (!read_val(file, upper_beam_deprecated)) {
        return Status::Corruption("Failed to read upper_beam from: " + index_path);
    }

    // --- Phase 4: Read storage (IndexFlat) ---
    // IndexHNSW stores vectors in an inner IndexFlat (the "storage" field).
    // Format: fourcc + index_header + WRITEXBVECTOR(codes)
    uint32_t storage_fourcc;
    if (!read_val(file, storage_fourcc)) {
        return Status::Corruption("Cannot read storage fourcc from: " + index_path);
    }

    // storage_fourcc == fourcc("null") means IO_FLAG_SKIP_STORAGE was used
    constexpr uint32_t kFourCC_null = make_fourcc('n', 'u', 'l', 'l');

    if (storage_fourcc == kFourCC_null) {
        // No storage — vectors were skipped during write
        LOG(INFO) << "HNSWGraphAccessor: storage was null (IO_FLAG_SKIP_STORAGE) in: " << index_path;
    } else if (is_flat_fourcc(storage_fourcc)) {
        FaissIndexHeader flat_hdr;
        if (!read_index_header(file, flat_hdr)) {
            return Status::Corruption("Cannot read IndexFlat header from: " + index_path);
        }

        // WRITEXBVECTOR(codes): writes size_t (count/4) then count bytes.
        // For IndexFlat, codes = uint8_t[ntotal * d * sizeof(float)].
        // WRITEXBVECTOR divides codes.size() by 4 before writing the count,
        // so the stored value = ntotal * d. Actual bytes = stored_value * 4.
        size_t xb_size;
        if (!read_val(file, xb_size)) {
            return Status::Corruption("Cannot read codes size from: " + index_path);
        }
        size_t nbytes = xb_size * 4; // WRITEXBVECTOR stores size/4

        if (load_vectors && nbytes > 0) {
            size_t num_floats = nbytes / sizeof(float);
            _stored_vectors.resize(num_floats);
            if (!file.read(reinterpret_cast<char*>(_stored_vectors.data()), nbytes)) {
                _stored_vectors.clear();
                return Status::Corruption("Failed to read vector data from: " + index_path);
            }
        } else if (nbytes > 0) {
            if (!skip_bytes(file, nbytes)) {
                return Status::Corruption("Failed to skip vector data in: " + index_path);
            }
        }
    } else {
        return Status::NotSupported(
                fmt::format("Unsupported HNSW storage type '{}' (fourcc=0x{:08X}) in: {}",
                            fourcc_to_string(storage_fourcc), storage_fourcc, index_path));
    }

    // --- Phase 5: Read id_map if IndexIDMap wrapping was present ---
    // After the inner index, IndexIDMap writes: WRITEVECTOR(id_map) → int64_t[]
    if (has_idmap) {
        if (!read_vector(file, _id_map)) {
            return Status::Corruption("Failed to read id_map from IndexIDMap in: " + index_path);
        }
        LOG(INFO) << "HNSWGraphAccessor: loaded id_map with " << _id_map.size() << " entries from: " << index_path;
    }

    // --- Phase 6: Derive M and validate ---
    if (!_cum_nneighbor_per_level.empty()) {
        _M = _cum_nneighbor_per_level[0] / 2;
    }

    if (_levels.empty()) {
        return Status::Corruption("HNSW graph has no nodes in: " + index_path);
    }
    if (_entry_point < 0 || _entry_point >= static_cast<node_id_t>(_levels.size())) {
        return Status::Corruption(fmt::format("Invalid entry point {} (num_nodes={}) in: {}", _entry_point,
                                              _levels.size(), index_path));
    }
    if (_offsets.size() != _levels.size() + 1) {
        return Status::Corruption(fmt::format("Offsets size mismatch: offsets={} levels={} in: {}", _offsets.size(),
                                              _levels.size(), index_path));
    }

    LOG(INFO) << "HNSWGraphAccessor loaded: ntotal=" << _ntotal << " dim=" << _dim << " M=" << _M
              << " max_level=" << _max_level << " entry_point=" << _entry_point << " has_id_map=" << has_id_map()
              << " has_vectors=" << has_stored_vectors() << " from: " << index_path;

    _valid = true;
    return Status::OK();
}

int64_t HNSWGraphAccessor::map_to_external_id(node_id_t internal_id) const {
    if (_id_map.empty()) {
        return static_cast<int64_t>(internal_id);
    }
    if (internal_id < 0 || static_cast<size_t>(internal_id) >= _id_map.size()) {
        return -1;
    }
    return _id_map[internal_id];
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
