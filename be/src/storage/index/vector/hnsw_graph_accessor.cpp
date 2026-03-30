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

#include "common/logging.h"
#include "fmt/format.h"

#ifdef WITH_TENANN
#include "faiss/IndexFlat.h"
#include "faiss/IndexHNSW.h"
#include "faiss/IndexIDMap.h"
#include "faiss/IndexPreTransform.h"
#include "faiss/impl/FaissException.h"
#include "faiss/index_io.h"
#endif

namespace starrocks {

Status HNSWGraphAccessor::init(const std::string& index_path, bool load_vectors) {
#ifdef WITH_TENANN
    try {
        std::unique_ptr<faiss::Index> raw_index(faiss::read_index(index_path.c_str()));
        if (!raw_index) {
            return Status::Corruption("faiss::read_index returned null for: " + index_path);
        }

        // Peel off wrapping layers to reach the inner IndexHNSW.
        // TenANN may produce: IndexIDMap(IndexHNSW), IndexPreTransform(IndexHNSW),
        // IndexIDMap(IndexPreTransform(IndexHNSW)), or plain IndexHNSW.
        const faiss::Index* inner = raw_index.get();

        // Layer 1: IndexIDMap / IndexIDMap2
        const auto* id_map = dynamic_cast<const faiss::IndexIDMap*>(inner);
        if (id_map) {
            _id_map.assign(id_map->id_map.begin(), id_map->id_map.end());
            inner = id_map->index;
            LOG(INFO) << "HNSWGraphAccessor: unwrapped IndexIDMap, id_map size=" << _id_map.size();
        }

        // Layer 2: IndexPreTransform (e.g. L2Norm for cosine similarity)
        const auto* pre_transform = dynamic_cast<const faiss::IndexPreTransform*>(inner);
        if (pre_transform) {
            inner = pre_transform->index;
            LOG(INFO) << "HNSWGraphAccessor: unwrapped IndexPreTransform";
        }

        // The inner index must be IndexHNSW (or a subclass like IndexHNSWFlat)
        const auto* hnsw_index = dynamic_cast<const faiss::IndexHNSW*>(inner);
        if (!hnsw_index) {
            return Status::NotSupported(
                    "Inner Faiss index is not IndexHNSW. Cannot extract HNSW graph from: " + index_path);
        }

        const faiss::HNSW& hnsw = hnsw_index->hnsw;

        // Copy graph structure
        _assign_probas.assign(hnsw.assign_probas.begin(), hnsw.assign_probas.end());
        _cum_nneighbor_per_level.assign(hnsw.cum_nneighbor_per_level.begin(),
                                        hnsw.cum_nneighbor_per_level.end());
        _levels.assign(hnsw.levels.begin(), hnsw.levels.end());
        _offsets.assign(hnsw.offsets.begin(), hnsw.offsets.end());

        _neighbors.resize(hnsw.neighbors.size());
        for (size_t i = 0; i < hnsw.neighbors.size(); i++) {
            _neighbors[i] = static_cast<node_id_t>(hnsw.neighbors[i]);
        }

        _entry_point = static_cast<node_id_t>(hnsw.entry_point);
        _max_level = hnsw.max_level;
        _ef_construction = hnsw.efConstruction;
        _ef_search = hnsw.efSearch;

        if (_cum_nneighbor_per_level.size() >= 2) {
            _M = _cum_nneighbor_per_level[0] / 2;
        } else if (_cum_nneighbor_per_level.size() == 1) {
            _M = _cum_nneighbor_per_level[0] / 2;
        }

        _dim = hnsw_index->d;
        _ntotal = hnsw_index->ntotal;

        // Extract stored vectors from the flat storage
        if (load_vectors && hnsw_index->storage) {
            const auto* flat = dynamic_cast<const faiss::IndexFlat*>(hnsw_index->storage);
            if (flat && flat->ntotal > 0) {
                size_t total_floats = static_cast<size_t>(flat->ntotal) * flat->d;
                _stored_vectors.resize(total_floats);
                // Reconstruct all vectors into our buffer (safe across Faiss versions)
                for (int64_t i = 0; i < flat->ntotal; i++) {
                    flat->reconstruct(i, _stored_vectors.data() + i * flat->d);
                }
            }
        }

        // Validate
        if (_levels.empty()) {
            return Status::Corruption("HNSW graph has no nodes");
        }
        if (_entry_point < 0 || _entry_point >= static_cast<node_id_t>(_levels.size())) {
            return Status::Corruption("Invalid entry point");
        }
        if (_offsets.size() != _levels.size() + 1) {
            return Status::Corruption(
                    fmt::format("Offsets size mismatch: offsets.size()={} levels.size()={}", _offsets.size(),
                                _levels.size()));
        }

        LOG(INFO) << "HNSWGraphAccessor loaded: ntotal=" << _ntotal << " dim=" << _dim << " M=" << _M
                  << " max_level=" << _max_level << " entry_point=" << _entry_point
                  << " has_id_map=" << has_id_map() << " has_vectors=" << has_stored_vectors();

        _valid = true;
        return Status::OK();

    } catch (const faiss::FaissException& e) {
        return Status::InternalError(fmt::format("Faiss error reading {}: {}", index_path, e.what()));
    } catch (const std::exception& e) {
        return Status::InternalError(fmt::format("Error reading {}: {}", index_path, e.what()));
    }
#else
    return Status::NotSupported("HNSWGraphAccessor requires WITH_TENANN build");
#endif
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
