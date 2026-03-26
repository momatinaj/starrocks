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

#include "storage/index/vector/acorn_index_reader.h"

#include <algorithm>
#include <cmath>
#include <queue>
#include <unordered_set>

namespace starrocks {

Status AcornIndexReader::init(const std::string& index_path) {
    RETURN_IF_ERROR(_graph.init(index_path, true));
    // Auto-load vectors from the .vi file if available
    if (_graph.has_stored_vectors()) {
        _vectors = _graph.stored_vectors();
        _num_rows = static_cast<int>(_graph.ntotal());
        _dim = _graph.dimension();
    }
    return Status::OK();
}

void AcornIndexReader::set_vector_data(const float* vectors, int num_rows, int dim) {
    _vectors = vectors;
    _num_rows = num_rows;
    _dim = dim;
}

void AcornIndexReader::set_predicate_evaluator(std::unique_ptr<SearchPredicateEvaluator> evaluator) {
    _predicate = std::move(evaluator);
}

float AcornIndexReader::_compute_distance(const float* query, node_id_t node_id) const {
    if (!_vectors || node_id < 0 || node_id >= _num_rows) {
        return std::numeric_limits<float>::max();
    }
    const float* vec = _vectors + static_cast<size_t>(node_id) * _dim;
    float sum = 0.0f;
    for (int i = 0; i < _dim; i++) {
        float diff = query[i] - vec[i];
        sum += diff * diff;
    }
    return sum;
}

std::vector<AcornIndexReader::node_id_t> AcornIndexReader::_get_neighbors_acorn(node_id_t node_id, int level) {
    if (!_predicate) {
        return _graph.neighbors(node_id, level);
    }

    // ACORN-1: 2-hop expansion + predicate filter
    auto expanded = _graph.expanded_neighbors(node_id, level);

    std::vector<node_id_t> filtered;
    filtered.reserve(expanded.size());
    for (auto n : expanded) {
        if (_predicate->evaluate(n)) {
            filtered.push_back(n);
        }
    }

    // Truncate to M neighbors (graph degree bound)
    int M = _graph.M();
    int max_neighbors = (level == 0) ? 2 * M : M;
    if (static_cast<int>(filtered.size()) > max_neighbors) {
        filtered.resize(max_neighbors);
    }

    return filtered;
}

std::vector<AcornIndexReader::NodeDist> AcornIndexReader::_search_layer(const float* query, node_id_t entry, int ef,
                                                                        int level) {
    // Min-heap for candidates (closest first)
    auto cmp_min = [](const NodeDist& a, const NodeDist& b) { return a.distance > b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_min)> candidates(cmp_min);

    // Max-heap for results (farthest first, so we can pop the worst)
    auto cmp_max = [](const NodeDist& a, const NodeDist& b) { return a.distance < b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_max)> results(cmp_max);

    std::unordered_set<node_id_t> visited;

    float d = _compute_distance(query, entry);
    candidates.push({entry, d});
    results.push({entry, d});
    visited.insert(entry);

    while (!candidates.empty()) {
        auto current = candidates.top();
        candidates.pop();

        // If current candidate is farther than the worst in results, stop
        if (current.distance > results.top().distance && static_cast<int>(results.size()) >= ef) {
            break;
        }

        // Get neighbors using ACORN-1's modified expansion
        auto nbrs = _get_neighbors_acorn(current.id, level);

        for (auto n : nbrs) {
            if (visited.count(n) > 0) continue;
            visited.insert(n);

            float dist = _compute_distance(query, n);

            if (static_cast<int>(results.size()) < ef || dist < results.top().distance) {
                candidates.push({n, dist});
                results.push({n, dist});
                if (static_cast<int>(results.size()) > ef) {
                    results.pop();
                }
            }
        }
    }

    // Extract results sorted by distance
    std::vector<NodeDist> sorted_results;
    sorted_results.reserve(results.size());
    while (!results.empty()) {
        sorted_results.push_back(results.top());
        results.pop();
    }
    std::sort(sorted_results.begin(), sorted_results.end());
    return sorted_results;
}

void AcornIndexReader::_search_multi_level(const float* query, int k, int ef_search, SearchResult& result) {
    node_id_t entry = _graph.entry_point();
    int top_level = _graph.max_level();

    // Traverse from top level down to level 1 with ef=1
    for (int level = top_level; level >= 1; level--) {
        auto layer_result = _search_layer(query, entry, 1, level);
        if (!layer_result.empty()) {
            entry = layer_result[0].id;
        }
    }

    // Search at level 0 with ef=ef_search
    auto candidates = _search_layer(query, entry, ef_search, 0);

    // If predicate is active, filter results to only predicate-satisfying nodes
    std::vector<NodeDist> filtered;
    for (const auto& nd : candidates) {
        if (!_predicate || _predicate->evaluate(nd.id)) {
            filtered.push_back(nd);
        }
    }

    // Return top-k
    int result_count = std::min(k, static_cast<int>(filtered.size()));
    result.row_ids.reserve(result_count);
    result.distances.reserve(result_count);
    for (int i = 0; i < result_count; i++) {
        result.row_ids.push_back(filtered[i].id);
        result.distances.push_back(filtered[i].distance);
    }
}

Status AcornIndexReader::search(const float* query_vector, const SearchParams& params, SearchResult& result) {
    if (!_graph.is_valid()) {
        return Status::InternalError("ACORN reader not initialized");
    }
    if (!_vectors || _dim <= 0) {
        return Status::InternalError("Vector data not set");
    }

    result.row_ids.clear();
    result.distances.clear();

    _search_multi_level(query_vector, params.k, params.ef_search, result);

    return Status::OK();
}

} // namespace starrocks
