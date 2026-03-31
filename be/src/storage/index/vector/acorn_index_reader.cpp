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

void AcornIndexReader::_heap_push(std::vector<int64_t>& ids, std::vector<float>& dists, int& nres, int max_size,
                                  int64_t id, float dist) {
    if (nres < max_size) {
        ids.push_back(id);
        dists.push_back(dist);
        nres++;
        // sift up to maintain max-heap (worst distance at [0])
        int i = nres - 1;
        while (i > 0) {
            int parent = (i - 1) / 2;
            if (dists[parent] < dists[i]) {
                std::swap(dists[parent], dists[i]);
                std::swap(ids[parent], ids[i]);
                i = parent;
            } else {
                break;
            }
        }
    } else if (dist < dists[0]) {
        // Replace the worst (root of max-heap) and sift down
        dists[0] = dist;
        ids[0] = id;
        int i = 0;
        while (true) {
            int l = 2 * i + 1, r = 2 * i + 2;
            int largest = i;
            if (l < nres && dists[l] > dists[largest]) largest = l;
            if (r < nres && dists[r] > dists[largest]) largest = r;
            if (largest == i) break;
            std::swap(dists[i], dists[largest]);
            std::swap(ids[i], ids[largest]);
            i = largest;
        }
    }
}

// Faithful reimplementation of hybrid_search_from_candidates from the
// reference ACORN code (acorn_hnsw.cpp).  Key properties preserved:
//   1. Only predicate-satisfying nodes are marked visited.
//   2. Per-neighbor 1-hop + 2-hop expansion (not pre-computed flat set).
//   3. num_found >= 2*M cap per expansion step.
//   4. nstep-based termination (nstep > efSearch).
std::vector<AcornIndexReader::NodeDist> AcornIndexReader::_search_layer_acorn(const float* query, node_id_t entry,
                                                                              int ef, int level) {
    auto cmp_min = [](const NodeDist& a, const NodeDist& b) { return a.distance > b.distance; };
    std::priority_queue<NodeDist, std::vector<NodeDist>, decltype(cmp_min)> candidates(cmp_min);

    std::unordered_set<node_id_t> visited;

    // Result heap: top-k by distance (max-heap, worst at top)
    std::vector<int64_t> res_ids;
    std::vector<float> res_dists;
    int nres = 0;

    int graph_M = _graph.M();
    int base_M = (_gamma > 1) ? (graph_M / _gamma) : graph_M;
    int ndis = 0;

    // Seed candidates with the entry point (matching reference lines 1424-1436)
    float d_entry = _compute_distance(query, entry);
    candidates.push({entry, d_entry});
    visited.insert(entry);
    if (_satisfies_predicate(entry)) {
        res_ids.push_back(entry);
        res_dists.push_back(d_entry);
        nres = 1;
    }

    int nstep = 0;

    while (!candidates.empty()) {
        auto current = candidates.top();
        candidates.pop();

        // Reference lines 1456-1535: expand v0's 1-hop neighbors
        auto one_hop = _graph.neighbors(current.id, level);

        if (nstep == 0) {
            LOG(INFO) << "ACORN expand: current=" << current.id
                      << " 1hop_size=" << one_hop.size()
                      << " node_level=" << _graph.node_level(current.id);
        }

        int num_found = 0;
        for (auto v1 : one_hop) {
            if (v1 < 0) break;

            bool v1_qualifies = _satisfies_predicate(v1);
            if (v1_qualifies) num_found++;

            // Reference: only skip if v1 is already visited (and only qualifying
            // nodes get marked visited, so non-qualifying are never in this set).
            if (visited.count(v1) > 0) {
                // Still do 2-hop expansion below even if v1 was visited
            } else if (v1_qualifies) {
                // Reference lines 1480-1496: qualifying + not visited
                visited.insert(v1);
                ndis++;
                float d = _compute_distance(query, v1);
                // Add to result heap (max-heap of size k)
                _heap_push(res_ids, res_dists, nres, ef, v1, d);
                candidates.push({v1, d});
                if (num_found >= 2 * base_M) break;
            }

            // Reference lines 1499-1533: 2-hop expansion for EVERY v1
            // (regardless of whether v1 satisfies the predicate)
            auto two_hop = _graph.neighbors(v1, level);
            for (auto v2 : two_hop) {
                if (v2 < 0) break;

                bool v2_qualifies = _satisfies_predicate(v2);
                if (v2_qualifies) {
                    num_found++;
                } else {
                    continue;  // reference line 1512
                }

                if (visited.count(v2) > 0) continue;
                visited.insert(v2);
                ndis++;
                float d2 = _compute_distance(query, v2);
                _heap_push(res_ids, res_dists, nres, ef, v2, d2);
                candidates.push({v2, d2});
                if (num_found >= 2 * base_M) break;
            }
        }

        nstep++;
        if (nstep > ef) break;  // reference line 1539
    }

    // Convert result heap to sorted output
    std::vector<NodeDist> sorted;
    sorted.reserve(nres);
    for (int i = 0; i < nres; i++) {
        sorted.push_back({static_cast<node_id_t>(res_ids[i]), res_dists[i]});
    }
    std::sort(sorted.begin(), sorted.end());

    LOG(INFO) << "ACORN _search_layer_acorn stats: nstep=" << nstep
              << " ndis=" << ndis
              << " qualifying_found=" << nres
              << " visited_size=" << visited.size()
              << " ef=" << ef
              << " gamma=" << _gamma
              << " graph_M=" << graph_M
              << " base_M=" << base_M;
    return sorted;
}

void AcornIndexReader::_search_multi_level(const float* query, int k, int ef_search, SearchResult& result) {
    node_id_t entry = _graph.entry_point();
    int top_level = _graph.max_level();

    LOG(INFO) << "ACORN _search_multi_level: k=" << k << " ef_search=" << ef_search
              << " entry=" << entry << " top_level=" << top_level
              << " has_predicate=" << (_predicate != nullptr)
              << " num_nodes=" << _num_rows << " dim=" << _dim
              << " gamma=" << _gamma;

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
