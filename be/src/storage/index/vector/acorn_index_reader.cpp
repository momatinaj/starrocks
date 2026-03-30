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
    bool result = ext_id >= 0 && _predicate->evaluate(ext_id);
    static thread_local int pred_call_count = 0;
    static thread_local int pred_pass_count = 0;
    pred_call_count++;
    if (result) pred_pass_count++;
    if (pred_call_count % 5000 == 0) {
        LOG(INFO) << "ACORN predicate stats: calls=" << pred_call_count
                  << " pass=" << pred_pass_count
                  << " rate=" << (100.0 * pred_pass_count / pred_call_count) << "%"
                  << " node_id=" << node_id << " ext_id=" << ext_id;
    }
    return result;
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

// ACORN-1 search with strict predicate-aware traversal — used for level 0.
//
// Key design (exactly matching ACORN paper Algorithm 2/3 for gamma=1):
//   - Uses 2-hop expanded neighbor lists to look across the graph.
//   - ONLY predicate-satisfying nodes are added to the candidate queue (C).
//   - This prevents the search from wasting steps on non-qualifying nodes.
//   - Non-qualifying nodes are implicitly used as "stepping stones" because
//     they are traversed to find the 2-hop neighbors, but they NEVER enter
//     the candidate queue themselves.
std::vector<AcornIndexReader::NodeDist> AcornIndexReader::_search_layer_acorn(const float* query, node_id_t entry,
                                                                              int ef, int level) {
    auto cmp_min = [](const NodeDist& a, const NodeDist& b) { return a.distance > b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_min)> candidates(cmp_min);

    // Result set: only predicate-satisfying nodes
    auto cmp_max = [](const NodeDist& a, const NodeDist& b) { return a.distance < b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_max)> results(cmp_max);

    std::unordered_set<node_id_t> visited;

    float d = _compute_distance(query, entry);
    // The entry point might not satisfy the predicate, but we must push it to
    // candidates to start the search. It acts as the initial stepping stone.
    candidates.push({entry, d});
    if (_satisfies_predicate(entry)) {
        results.push({entry, d});
    }
    visited.insert(entry);

    // Limit total visited nodes to prevent runaway search with very selective predicates.
    // With ef=400, this caps at 4000 nodes.
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

        // ACORN-1 logic: Look at all nodes within 2 hops (gamma=1).
        auto expanded = _graph.expanded_neighbors(current.id, level);

        if (visit_count <= 2) {
            LOG(INFO) << "ACORN expand: current=" << current.id
                      << " expanded_size=" << expanded.size()
                      << " 1hop_size=" << _graph.neighbors(current.id, level).size();
        }

        for (auto n : expanded) {
            if (visited.count(n) > 0) continue;
            visited.insert(n);
            visit_count++;

            // CRITICAL: We ONLY process nodes that satisfy the predicate.
            // Non-qualifying nodes are completely ignored here (they were
            // already used implicitly as bridges to find the 2-hop neighbors).
            if (_satisfies_predicate(n)) {
                float dist = _compute_distance(query, n);
                
                float worst = results.empty() ? std::numeric_limits<float>::max() : results.top().distance;
                if (static_cast<int>(results.size()) < ef || dist < worst) {
                    // Valid node: add to BOTH candidates (to continue search) and results
                    candidates.push({n, dist});
                    results.push({n, dist});
                    if (static_cast<int>(results.size()) > ef) {
                        results.pop();
                    }
                }
            }
        }
    }

    int total_qualifying = 0;
    std::vector<NodeDist> sorted;
    sorted.reserve(results.size());
    while (!results.empty()) {
        sorted.push_back(results.top());
        results.pop();
        total_qualifying++;
    }
    std::sort(sorted.begin(), sorted.end());
    LOG(INFO) << "ACORN _search_layer_acorn stats: visited=" << visit_count
              << "/" << max_visits
              << " qualifying_found=" << total_qualifying
              << " candidates_remaining=" << candidates.size()
              << " ef=" << ef;
    return sorted;
}

void AcornIndexReader::_search_multi_level(const float* query, int k, int ef_search, SearchResult& result) {
    node_id_t entry = _graph.entry_point();
    int top_level = _graph.max_level();

    LOG(INFO) << "ACORN _search_multi_level: k=" << k << " ef_search=" << ef_search
              << " entry=" << entry << " top_level=" << top_level
              << " has_predicate=" << (_predicate != nullptr)
              << " num_nodes=" << _num_rows << " dim=" << _dim;

    for (int level = top_level; level >= 1; level--) {
        auto layer_result = _search_layer_standard(query, entry, 1, level);
        if (!layer_result.empty()) {
            entry = layer_result[0].id;
        }
    }
    LOG(INFO) << "ACORN after upper-level search: entry=" << entry;

    int acorn_ef = _predicate ? std::max(ef_search, k * 10) : ef_search;
    LOG(INFO) << "ACORN level-0 search: acorn_ef=" << acorn_ef
              << " using " << (_predicate ? "ACORN-1 (predicate)" : "standard");

    // Diagnostic: check if level-0 neighbors exist for the entry point
    {
        auto entry_nbrs = _graph.neighbors(entry, 0);
        int entry_level = _graph.node_level(entry);
        LOG(INFO) << "ACORN diag: entry=" << entry
                  << " node_level=" << entry_level
                  << " level0_neighbors=" << entry_nbrs.size()
                  << " graph.num_nodes=" << _graph.num_nodes()
                  << " graph.max_level=" << _graph.max_level()
                  << " graph.M=" << _graph.M();
        if (!entry_nbrs.empty()) {
            std::string first_few;
            for (int ii = 0; ii < std::min(5, static_cast<int>(entry_nbrs.size())); ii++) {
                if (ii > 0) first_few += ",";
                first_few += std::to_string(entry_nbrs[ii]);
            }
            LOG(INFO) << "ACORN diag: first neighbors=[" << first_few << "]";
        }
    }

    auto candidates = _predicate ? _search_layer_acorn(query, entry, acorn_ef, 0)
                                 : _search_layer_standard(query, entry, ef_search, 0);

    LOG(INFO) << "ACORN level-0 returned " << candidates.size() << " candidates";

    int count = 0;
    for (const auto& nd : candidates) {
        if (count >= k) break;
        if (_satisfies_predicate(nd.id)) {
            int64_t ext_id = _graph.map_to_external_id(nd.id);
            result.row_ids.push_back(ext_id);
            result.distances.push_back(nd.distance);
            count++;
        }
    }
    LOG(INFO) << "ACORN final result: " << count << " qualifying rows out of "
              << candidates.size() << " candidates";
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
