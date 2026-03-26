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

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <numeric>
#include <random>
#include <unordered_set>
#include <vector>

#include "fs/fs.h"
#include "storage/index/vector/search_predicate_evaluator.h"
#include "testutil/assert.h"

namespace starrocks {

class AcornIndexReaderTest : public testing::Test {
public:
    static constexpr int kDim = 8;
    static constexpr int kM = 4;
    static constexpr int kNumNodes = 100;

protected:
    void SetUp() override {
        CHECK_OK(fs::remove_all(test_dir));
        CHECK_OK(fs::create_directories(test_dir));
        _gen.seed(42);
    }

    void TearDown() override { fs::remove_all(test_dir); }

    const std::string test_dir = "acorn_index_reader_test";
    std::mt19937 _gen;

    // Generate random vectors
    std::vector<float> generate_random_vectors(int n, int dim) {
        std::vector<float> vecs(n * dim);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (auto& v : vecs) v = dist(_gen);
        return vecs;
    }

    // Generate lat/lng around a center point with varying distances
    void generate_geo_data(int n, double center_lat, double center_lng, double spread_km,
                           std::vector<double>& lats, std::vector<double>& lngs) {
        lats.resize(n);
        lngs.resize(n);
        std::uniform_real_distribution<double> dist(-spread_km, spread_km);
        for (int i = 0; i < n; i++) {
            double dlat = dist(_gen) / 111.0; // ~1 degree ≈ 111 km
            double dlng = dist(_gen) / (111.0 * std::cos(center_lat * M_PI / 180.0));
            lats[i] = center_lat + dlat;
            lngs[i] = center_lng + dlng;
        }
    }

    void _write_graph_section(std::ofstream& ofs, int n, int M, int dim,
                              const std::vector<int>& levels) {
        std::vector<double> assign_probas = {0.5, 0.25};
        uint64_t sz = assign_probas.size();
        ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
        ofs.write(reinterpret_cast<const char*>(assign_probas.data()), sz * sizeof(double));

        int slots0 = 2 * M;
        int slots1 = M;
        std::vector<int> cum_nn = {slots0, slots0 + slots1};
        sz = cum_nn.size();
        ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
        ofs.write(reinterpret_cast<const char*>(cum_nn.data()), sz * sizeof(int));

        sz = levels.size();
        ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
        ofs.write(reinterpret_cast<const char*>(levels.data()), sz * sizeof(int));

        std::uniform_int_distribution<int> rdist(0, n - 1);
        std::vector<size_t> offsets;
        std::vector<int32_t> neighbors;
        size_t offset = 0;

        for (int i = 0; i < n; i++) {
            offsets.push_back(offset);
            int total_slots = (levels[i] == 2) ? (slots0 + slots1) : slots0;

            std::unordered_set<int> used;
            used.insert(i);
            auto add_neighbor = [&](int nbr) {
                if (nbr != i && used.count(nbr) == 0 && static_cast<int>(used.size()) <= slots0) {
                    neighbors.push_back(nbr);
                    used.insert(nbr);
                    return true;
                }
                return false;
            };

            add_neighbor((i + 1) % n);
            add_neighbor((i + n - 1) % n);

            for (int j = 0; j < M - 2 && static_cast<int>(used.size()) <= slots0; j++) {
                add_neighbor(rdist(_gen));
            }

            while (static_cast<int>(neighbors.size()) < static_cast<int>(offset) + slots0) {
                neighbors.push_back(-1);
            }

            if (levels[i] == 2) {
                for (int j = 0; j < slots1; j++) {
                    neighbors.push_back(-1);
                }
            }

            offset += total_slots;
        }
        offsets.push_back(offset);

        sz = offsets.size();
        ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
        ofs.write(reinterpret_cast<const char*>(offsets.data()), sz * sizeof(size_t));

        sz = neighbors.size();
        ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
        ofs.write(reinterpret_cast<const char*>(neighbors.data()), sz * sizeof(int32_t));

        int32_t entry = 0;
        int max_level = 1;
        int efc = 40;
        int efs = 16;
        int ub = 1;
        ofs.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
        ofs.write(reinterpret_cast<const char*>(&max_level), sizeof(max_level));
        ofs.write(reinterpret_cast<const char*>(&efc), sizeof(efc));
        ofs.write(reinterpret_cast<const char*>(&efs), sizeof(efs));
        ofs.write(reinterpret_cast<const char*>(&ub), sizeof(ub));
    }

    void _write_index_header(std::ofstream& ofs, int dim, int64_t ntotal) {
        uint32_t fourcc = 0x664E4849; // "IHNf"
        ofs.write(reinterpret_cast<const char*>(&fourcc), sizeof(fourcc));

        int d = dim;
        int64_t dummy = 0;
        bool is_trained = true;
        int metric_type = 1;
        ofs.write(reinterpret_cast<const char*>(&d), sizeof(d));
        ofs.write(reinterpret_cast<const char*>(&ntotal), sizeof(ntotal));
        ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
        ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
        ofs.write(reinterpret_cast<const char*>(&is_trained), sizeof(is_trained));
        ofs.write(reinterpret_cast<const char*>(&metric_type), sizeof(metric_type));
    }

    // Build a graph-only HNSW .vi file (no vectors stored).
    std::string build_synthetic_hnsw_graph_only(int n, int M, int dim) {
        std::string path = test_dir + "/acorn_graph_only.vi";
        std::ofstream ofs(path, std::ios::binary);

        _write_index_header(ofs, dim, n);

        std::vector<int> levels(n, 1);
        levels[0] = 2;
        _write_graph_section(ofs, n, M, dim, levels);

        ofs.close();
        return path;
    }

    // Build a full IndexHNSWFlat .vi file with graph + flat vector storage.
    std::string build_synthetic_hnsw(int n, int M, int dim,
                                     const std::vector<float>* vectors = nullptr) {
        std::string path = test_dir + "/acorn_test.vi";
        std::ofstream ofs(path, std::ios::binary);

        _write_index_header(ofs, dim, n);

        std::vector<int> levels(n, 1);
        levels[0] = 2;
        _write_graph_section(ofs, n, M, dim, levels);

        // Write IndexFlat storage section
        uint32_t storage_fourcc = 0x6C467849; // "IxFl"
        ofs.write(reinterpret_cast<const char*>(&storage_fourcc), sizeof(storage_fourcc));

        // Storage index header
        int d = dim;
        int64_t ntotal = n;
        int64_t dummy = 0;
        bool is_trained = true;
        int metric_type = 1;
        ofs.write(reinterpret_cast<const char*>(&d), sizeof(d));
        ofs.write(reinterpret_cast<const char*>(&ntotal), sizeof(ntotal));
        ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
        ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
        ofs.write(reinterpret_cast<const char*>(&is_trained), sizeof(is_trained));
        ofs.write(reinterpret_cast<const char*>(&metric_type), sizeof(metric_type));

        // Codes vector (float vectors stored as uint8_t)
        std::vector<float> vec_data;
        if (vectors && static_cast<int>(vectors->size()) == n * dim) {
            vec_data = *vectors;
        } else {
            vec_data = generate_random_vectors(n, dim);
        }
        uint64_t byte_count = static_cast<uint64_t>(vec_data.size()) * sizeof(float);
        ofs.write(reinterpret_cast<const char*>(&byte_count), sizeof(byte_count));
        ofs.write(reinterpret_cast<const char*>(vec_data.data()), byte_count);

        ofs.close();
        return path;
    }

    // Brute-force ground truth: top-k nearest by L2 distance
    std::vector<int> brute_force_knn(const float* vectors, int n, int dim, const float* query, int k) {
        std::vector<std::pair<float, int>> dists;
        for (int i = 0; i < n; i++) {
            float d = 0;
            for (int j = 0; j < dim; j++) {
                float diff = query[j] - vectors[i * dim + j];
                d += diff * diff;
            }
            dists.push_back({d, i});
        }
        std::sort(dists.begin(), dists.end());
        std::vector<int> result;
        for (int i = 0; i < std::min(k, n); i++) {
            result.push_back(dists[i].second);
        }
        return result;
    }

    // Compute recall: fraction of ground truth IDs found in result
    double compute_recall(const std::vector<int64_t>& result_ids, const std::vector<int>& ground_truth_ids) {
        std::unordered_set<int> gt_set(ground_truth_ids.begin(), ground_truth_ids.end());
        int found = 0;
        for (auto id : result_ids) {
            if (gt_set.count(static_cast<int>(id)) > 0) found++;
        }
        return ground_truth_ids.empty() ? 0.0 : static_cast<double>(found) / ground_truth_ids.size();
    }
};

TEST_F(AcornIndexReaderTest, test_search_without_predicate) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_synthetic_hnsw(kNumNodes, kM, kDim, &vectors);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    // Vectors are auto-loaded from the .vi file
    ASSERT_EQ(reader.dimension(), kDim);
    ASSERT_EQ(reader.num_nodes(), kNumNodes);

    float query[kDim];
    std::copy(vectors.begin(), vectors.begin() + kDim, query);

    AcornIndexReader::SearchParams params;
    params.k = 5;
    params.ef_search = 20;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    ASSERT_GT(result.row_ids.size(), 0);
    ASSERT_LE(result.row_ids.size(), 5);
    // First result should be node 0 (exact match with query = vectors[0])
    ASSERT_EQ(result.row_ids[0], 0);
    ASSERT_FLOAT_EQ(result.distances[0], 0.0f);
}

TEST_F(AcornIndexReaderTest, test_search_with_explicit_vector_data) {
    // Test the path where vectors are set explicitly (not from .vi file)
    auto path = build_synthetic_hnsw_graph_only(kNumNodes, kM, kDim);
    auto vectors = generate_random_vectors(kNumNodes, kDim);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    reader.set_vector_data(vectors.data(), kNumNodes, kDim);

    float query[kDim];
    std::copy(vectors.begin(), vectors.begin() + kDim, query);

    AcornIndexReader::SearchParams params;
    params.k = 5;
    params.ef_search = 20;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    ASSERT_GT(result.row_ids.size(), 0);
    ASSERT_LE(result.row_ids.size(), 5);
    ASSERT_EQ(result.row_ids[0], 0);
    ASSERT_FLOAT_EQ(result.distances[0], 0.0f);
}

TEST_F(AcornIndexReaderTest, test_search_with_radius_predicate) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_synthetic_hnsw(kNumNodes, kM, kDim, &vectors);

    // Generate geo data: half inside 5km, half far away
    std::vector<double> lats(kNumNodes), lngs(kNumNodes);
    for (int i = 0; i < kNumNodes; i++) {
        if (i % 2 == 0) {
            lats[i] = 37.7749 + (i * 0.0001);
            lngs[i] = -122.4194 + (i * 0.0001);
        } else {
            lats[i] = 40.0;
            lngs[i] = -100.0;
        }
    }

    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = 37.7749;
    spec.center_lng = -122.4194;
    spec.radius_meters = 50000;

    auto evaluator = create_predicate_evaluator(spec);
    ASSERT_NE(evaluator, nullptr);
    ASSERT_OK(evaluator->init(spec, lats, lngs));

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    reader.set_predicate_evaluator(std::move(evaluator));

    float query[kDim] = {0};
    AcornIndexReader::SearchParams params;
    params.k = 10;
    params.ef_search = 40;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    SpatialRadiusEvaluator checker;
    ASSERT_OK(checker.init(spec, lats, lngs));
    for (auto id : result.row_ids) {
        ASSERT_TRUE(checker.evaluate(id)) << "Row " << id << " does not satisfy radius predicate";
    }
}

TEST_F(AcornIndexReaderTest, test_search_empty_predicate_match) {
    auto path = build_synthetic_hnsw(kNumNodes, kM, kDim);

    std::vector<double> lats(kNumNodes, 80.0);
    std::vector<double> lngs(kNumNodes, 0.0);

    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = 37.7749;
    spec.center_lng = -122.4194;
    spec.radius_meters = 100;

    auto evaluator = create_predicate_evaluator(spec);
    ASSERT_OK(evaluator->init(spec, lats, lngs));

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    reader.set_predicate_evaluator(std::move(evaluator));

    float query[kDim] = {0};
    AcornIndexReader::SearchParams params;
    params.k = 10;
    params.ef_search = 40;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));
    ASSERT_EQ(result.row_ids.size(), 0);
}

TEST_F(AcornIndexReaderTest, test_search_all_predicate_match) {
    auto path = build_synthetic_hnsw(kNumNodes, kM, kDim);

    std::vector<double> lats(kNumNodes, 37.7749);
    std::vector<double> lngs(kNumNodes, -122.4194);

    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = 37.7749;
    spec.center_lng = -122.4194;
    spec.radius_meters = 1000;

    auto evaluator = create_predicate_evaluator(spec);
    ASSERT_OK(evaluator->init(spec, lats, lngs));

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    reader.set_predicate_evaluator(std::move(evaluator));

    float query[kDim] = {0};
    AcornIndexReader::SearchParams params;
    params.k = 10;
    params.ef_search = 40;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));
    ASSERT_EQ(result.row_ids.size(), 10);
}

TEST_F(AcornIndexReaderTest, test_uninitialized_reader) {
    AcornIndexReader reader;

    float query[kDim] = {0};
    AcornIndexReader::SearchParams params;
    AcornIndexReader::SearchResult result;

    auto status = reader.search(query, params, result);
    ASSERT_FALSE(status.ok());
}

TEST_F(AcornIndexReaderTest, test_no_vector_data) {
    // Use graph-only file so vectors are NOT auto-loaded
    auto path = build_synthetic_hnsw_graph_only(kNumNodes, kM, kDim);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    // Don't set vector data -- auto-load should not happen for graph-only file

    float query[kDim] = {0};
    AcornIndexReader::SearchParams params;
    AcornIndexReader::SearchResult result;

    auto status = reader.search(query, params, result);
    ASSERT_FALSE(status.ok());
}

} // namespace starrocks
