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
#include <limits>
#include <queue>
#include <unordered_set>

#include "common/logging.h"

namespace starrocks {

Status AcornIndexReader::init(const std::string& index_path) {
    RETURN_IF_ERROR(_graph.init(index_path, true));
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

bool AcornIndexReader::_satisfies_predicate(node_id_t node_id) const {
    if (!_predicate) return true;
    int64_t ext_id = _graph.map_to_external_id(node_id);
    return ext_id >= 0 && _predicate->evaluate(ext_id);
}

// Standard HNSW greedy search — used for upper levels (>= 1).
// NO predicate filtering. This finds the best entry point for level 0.
std::vector<AcornIndexReader::NodeDist> AcornIndexReader::_search_layer_standard(const float* query, node_id_t entry,
                                                                                 int ef, int level) {
    auto cmp_min = [](const NodeDist& a, const NodeDist& b) { return a.distance > b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_min)> candidates(cmp_min);

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

        if (current.distance > results.top().distance && static_cast<int>(results.size()) >= ef) {
            break;
        }

        auto nbrs = _graph.neighbors(current.id, level);
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

    std::vector<NodeDist> sorted;
    sorted.reserve(results.size());
    while (!results.empty()) {
        sorted.push_back(results.top());
        results.pop();
    }
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

// ACORN-1 search with hybrid exploration — used for level 0.
//
// Key design (per ACORN paper Algorithm 3, with practical enhancement):
//   - Uses 2-hop expanded neighbor lists for broader graph reach
//   - ALL expanded neighbors are explored as candidates (for graph navigation)
//   - Only predicate-satisfying nodes enter the RESULT set
//   - This prevents the search from dead-ending when the predicate is selective,
//     while still ensuring results satisfy the spatial constraint
std::vector<AcornIndexReader::NodeDist> AcornIndexReader::_search_layer_acorn(const float* query, node_id_t entry,
                                                                              int ef, int level) {
    auto cmp_min = [](const NodeDist& a, const NodeDist& b) { return a.distance > b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_min)> candidates(cmp_min);

    // Result set: only predicate-satisfying nodes
    auto cmp_max = [](const NodeDist& a, const NodeDist& b) { return a.distance < b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_max)> results(cmp_max);

    std::unordered_set<node_id_t> visited;

    float d = _compute_distance(query, entry);
    candidates.push({entry, d});
    if (_satisfies_predicate(entry)) {
        results.push({entry, d});
    }
    visited.insert(entry);

    // Limit total visited nodes to prevent runaway search with very selective predicates.
    // With 100K nodes and ef=400, this caps at ~4000 nodes (4% of data).
    const int max_visits = std::max(2000, ef * 10);
    int visit_count = 1;

    while (!candidates.empty() && visit_count < max_visits) {
        auto current = candidates.top();
        candidates.pop();

        // Termination: stop when the best remaining candidate is worse than the
        // worst result AND we have enough qualifying results
        if (!results.empty() && static_cast<int>(results.size()) >= ef &&
            current.distance > results.top().distance) {
            break;
        }

        // 2-hop expansion: explore ALL expanded neighbors (regardless of predicate)
        auto expanded = _graph.expanded_neighbors(current.id, level);

        for (auto n : expanded) {
            if (visited.count(n) > 0) continue;
            visited.insert(n);
            visit_count++;

            float dist = _compute_distance(query, n);

            if (_satisfies_predicate(n)) {
                // Qualifying node: add to both candidates (for navigation) and results
                float worst = results.empty() ? std::numeric_limits<float>::max() : results.top().distance;
                if (static_cast<int>(results.size()) < ef || dist < worst) {
                    candidates.push({n, dist});
                    results.push({n, dist});
                    if (static_cast<int>(results.size()) > ef) {
                        results.pop();
                    }
                }
            } else {
                // Non-qualifying node: add to candidates ONLY (stepping stone for navigation).
                // Only add if it's close enough to potentially lead to better qualifying nodes.
                float worst = results.empty() ? std::numeric_limits<float>::max() : results.top().distance;
                if (static_cast<int>(results.size()) < ef || dist < worst) {
                    candidates.push({n, dist});
                }
            }
        }
    }

    std::vector<NodeDist> sorted;
    sorted.reserve(results.size());
    while (!results.empty()) {
        sorted.push_back(results.top());
        results.pop();
    }
    std::sort(sorted.begin(), sorted.end());
    return sorted;
}

void AcornIndexReader::_search_multi_level(const float* query, int k, int ef_search, SearchResult& result) {
    node_id_t entry = _graph.entry_point();
    int top_level = _graph.max_level();

    // Phase 1: Upper levels — STANDARD HNSW greedy search, NO predicate.
    // Per ACORN Algorithm 2: only level 0 uses predicate-aware search.
    // This navigates to the best entry point close to the query vector.
    for (int level = top_level; level >= 1; level--) {
        auto layer_result = _search_layer_standard(query, entry, 1, level);
        if (!layer_result.empty()) {
            entry = layer_result[0].id;
        }
    }

    // Phase 2: Level 0 — ACORN-1 search with 2-hop expansion + predicate.
    // The caller (segment_iterator) already boosts ef_search for predicates;
    // we apply a moderate internal bump to ensure enough exploration.
    int acorn_ef = _predicate ? std::max(ef_search, k * 10) : ef_search;

    auto candidates = _predicate ? _search_layer_acorn(query, entry, acorn_ef, 0)
                                 : _search_layer_standard(query, entry, ef_search, 0);

    // Phase 3: Return top-k qualifying results.
    // _search_layer_acorn already ensures all results satisfy the predicate,
    // but we double-check for safety and map to external IDs.
    int count = 0;
    for (const auto& nd : candidates) {
        if (count >= k) break;
        if (_satisfies_predicate(nd.id)) {
            result.row_ids.push_back(_graph.map_to_external_id(nd.id));
            result.distances.push_back(nd.distance);
            count++;
        }
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
