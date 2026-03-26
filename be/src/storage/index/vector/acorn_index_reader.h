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
#include <vector>

#include "common/status.h"
#include "storage/index/vector/hnsw_graph_accessor.h"
#include "storage/index/vector/search_predicate_evaluator.h"

namespace starrocks {

// Implements ACORN-1 search (Algorithm 2 from the ACORN paper).
//
// ACORN-1 modifies standard HNSW search by expanding neighbor lists to include
// 2-hop neighbors, filtering by an arbitrary predicate, and traversing the
// resulting predicate-satisfying subgraph. Construction is identical to HNSW.
//
// This reader:
// 1. Loads the HNSW graph from the .vi file via HNSWGraphAccessor
// 2. Accepts pre-loaded vector data for distance computation
// 3. Accepts an optional SearchPredicateEvaluator for predicate filtering
// 4. Implements the ACORN-1 multi-level greedy search with 2-hop expansion
class AcornIndexReader {
public:
    using node_id_t = HNSWGraphAccessor::node_id_t;

    struct SearchResult {
        std::vector<int64_t> row_ids;
        std::vector<float> distances;
    };

    struct SearchParams {
        int k = 10;
        int ef_search = 40;
    };

    AcornIndexReader() = default;
    ~AcornIndexReader() = default;

    // Initialize from a .vi file path.
    Status init(const std::string& index_path);

    // Set pre-loaded vector data from segment columns (row_id -> float vector).
    // vectors: flat array of [num_rows * dim] floats.
    void set_vector_data(const float* vectors, int num_rows, int dim);

    // Set the predicate evaluator (optional; nullptr = no predicate filtering).
    void set_predicate_evaluator(std::unique_ptr<SearchPredicateEvaluator> evaluator);

    // Run ACORN-1 search.
    Status search(const float* query_vector, const SearchParams& params, SearchResult& result);

    bool is_valid() const { return _graph.is_valid(); }
    int num_nodes() const { return _graph.num_nodes(); }
    int dimension() const { return _dim; }

private:
    // ACORN-1 Algorithm 2: multi-level search
    void _search_multi_level(const float* query, int k, int ef_search, SearchResult& result);

    // Search at a single level with ef candidates.
    // Returns: candidate set sorted by distance.
    struct NodeDist {
        node_id_t id;
        float distance;
        bool operator<(const NodeDist& other) const { return distance < other.distance; }
        bool operator>(const NodeDist& other) const { return distance > other.distance; }
    };

    std::vector<NodeDist> _search_layer(const float* query, node_id_t entry, int ef, int level);

    // ACORN-1's modified GET-NEIGHBORS: 2-hop expansion + predicate filter.
    std::vector<node_id_t> _get_neighbors_acorn(node_id_t node_id, int level);

    // Compute L2 distance between query and stored vector at node_id.
    float _compute_distance(const float* query, node_id_t node_id) const;

    HNSWGraphAccessor _graph;
    std::unique_ptr<SearchPredicateEvaluator> _predicate;

    const float* _vectors = nullptr;
    int _num_rows = 0;
    int _dim = 0;
};

} // namespace starrocks
