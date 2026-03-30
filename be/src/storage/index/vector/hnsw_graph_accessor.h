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
#include <string>
#include <unordered_set>
#include <vector>

#include "common/status.h"

namespace starrocks {

// Reads the HNSW graph structure from a Faiss-format .vi file.
// Provides adjacency list access, 2-hop expansion, and basic graph queries
// for use by the ACORN-1 search algorithm.
//
// Parses the Faiss binary serialization format directly (no Faiss headers
// required at compile time). Handles IndexIDMap wrapping (produced by TenANN
// when nullable vector columns are used) and plain IndexHNSWFlat.
//
// When load_vectors=true is passed to init(), also extracts the stored
// flat vector data from the IndexFlat storage inside IndexHNSWFlat.
class HNSWGraphAccessor {
public:
    using node_id_t = int32_t;

    HNSWGraphAccessor() = default;
    ~HNSWGraphAccessor() = default;

    // Load the HNSW graph from a Faiss-format .vi file.
    // If load_vectors is true, also reads the flat vector storage.
    Status init(const std::string& index_path, bool load_vectors = false);

    // Returns the neighbor list for a node at the given level.
    std::vector<node_id_t> neighbors(node_id_t node_id, int level) const;

    // Returns the max level of a node (base level = 1 in Faiss convention).
    int node_level(node_id_t node_id) const;

    // Entry point node for top-level search.
    node_id_t entry_point() const { return _entry_point; }

    // Maximum level in the graph (0-indexed).
    int max_level() const { return _max_level; }

    // Total number of nodes.
    int num_nodes() const { return static_cast<int>(_levels.size()); }

    // Graph degree bound (max neighbors per node at level 0 = 2*M, higher levels = M).
    int M() const { return _M; }

    // Number of neighbors at a given layer.
    int nb_neighbors(int layer) const;

    // 2-hop expansion: returns union of all 1-hop and 2-hop neighbors,
    // deduplicated and excluding node_id itself.
    std::vector<node_id_t> expanded_neighbors(node_id_t node_id, int level) const;

    // Stored vector data (only populated when init(..., true) is used)
    const float* stored_vectors() const { return _stored_vectors.empty() ? nullptr : _stored_vectors.data(); }
    int dimension() const { return _dim; }
    int64_t ntotal() const { return _ntotal; }
    bool has_stored_vectors() const { return !_stored_vectors.empty(); }

    // ID mapping (from internal Faiss node IDs to external row IDs).
    // If no IndexIDMap wrapping was present, returns internal_id unchanged.
    int64_t map_to_external_id(node_id_t internal_id) const;
    bool has_id_map() const { return !_id_map.empty(); }

    bool is_valid() const { return _valid; }

private:
    // Compute neighbor range in the flat neighbors array for (node_id, level).
    void _neighbor_range(node_id_t node_id, int level, size_t& begin, size_t& end) const;

    // Graph data (matches faiss::HNSW layout)
    std::vector<double> _assign_probas;
    std::vector<int> _cum_nneighbor_per_level;
    std::vector<int> _levels;            // level of each node (base=1 in Faiss)
    std::vector<size_t> _offsets;         // offsets into _neighbors
    std::vector<node_id_t> _neighbors;    // flat neighbor array

    // ID mapping from IndexIDMap (internal_id -> external_row_id)
    std::vector<int64_t> _id_map;

    // Stored vectors (from IndexFlat storage, only when load_vectors=true)
    std::vector<float> _stored_vectors;
    int _dim = 0;
    int64_t _ntotal = 0;

    node_id_t _entry_point = -1;
    int _max_level = -1;
    int _ef_construction = 40;
    int _ef_search = 16;
    int _M = 16;

    bool _valid = false;
};

} // namespace starrocks
