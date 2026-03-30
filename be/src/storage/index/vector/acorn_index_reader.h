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

// Implements ACORN-1 search (Algorithm 2 from the ACORN paper,
// https://arxiv.org/abs/2403.04871).
//
// Key points of the algorithm:
//   - Upper levels (>= 1): standard HNSW greedy search (NO predicate).
//     This finds the best entry point near the query vector.
//   - Level 0: ACORN search with 2-hop neighbor expansion + predicate filter.
//     Non-qualifying nodes are still explored as navigation stepping stones,
//     but only qualifying nodes are kept in the result set.
//   - ef_search is boosted when a predicate is active, to compensate for
//     reduced effective graph connectivity under selective predicates.
//
// Construction is identical to standard HNSW — the graph is not modified.
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

    Status init(const std::string& index_path);

    void set_vector_data(const float* vectors, int num_rows, int dim);
    void set_predicate_evaluator(std::unique_ptr<SearchPredicateEvaluator> evaluator);

    Status search(const float* query_vector, const SearchParams& params, SearchResult& result);

    bool is_valid() const { return _graph.is_valid(); }
    bool has_predicate() const { return _predicate != nullptr; }
    int num_nodes() const { return _graph.num_nodes(); }
    int dimension() const { return _dim; }

private:
    void _search_multi_level(const float* query, int k, int ef_search, SearchResult& result);

    struct NodeDist {
        node_id_t id;
        float distance;
        bool operator<(const NodeDist& other) const { return distance < other.distance; }
        bool operator>(const NodeDist& other) const { return distance > other.distance; }
    };

    // Standard HNSW layer search (no predicate filtering). Used for upper levels.
    std::vector<NodeDist> _search_layer_standard(const float* query, node_id_t entry, int ef, int level);

    // ACORN-1 layer search with hybrid exploration. Used for level 0.
    // Explores through ALL 2-hop neighbors (for navigation), but only keeps
    // predicate-satisfying nodes in the result set.
    std::vector<NodeDist> _search_layer_acorn(const float* query, node_id_t entry, int ef, int level);

    bool _satisfies_predicate(node_id_t node_id) const;

    float _compute_distance(const float* query, node_id_t node_id) const;

    // Bounded max-heap push matching Faiss heap_addn_with_ids semantics.
    // Keeps the closest `max_size` nodes; rejects if full and dist >= worst.
    static void _heap_push(std::vector<int64_t>& ids, std::vector<float>& dists, int& nres, int max_size,
                           int64_t id, float dist);

    HNSWGraphAccessor _graph;
    std::unique_ptr<SearchPredicateEvaluator> _predicate;

    const float* _vectors = nullptr;
    int _num_rows = 0;
    int _dim = 0;
};

} // namespace starrocks
