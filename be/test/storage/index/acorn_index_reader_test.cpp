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

#ifdef WITH_TENANN

#include "storage/index/vector/acorn_index_reader.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <numeric>
#include <random>
#include <unordered_set>
#include <vector>

#include "fs/fs.h"
#include "fs/fs_util.h"
#include "hnsw_test_helper.h"
#include "storage/index/vector/search_predicate_evaluator.h"
#include "testutil/assert.h"

namespace starrocks {

class AcornIndexReaderTest : public testing::Test {
public:
    static constexpr int kDim = 8;
    static constexpr int kM = 16;
    static constexpr int kNumNodes = 200;

protected:
    void SetUp() override {
        CHECK_OK(fs::remove_all(test_dir));
        CHECK_OK(fs::create_directories(test_dir));
        _gen.seed(42);
    }

    void TearDown() override { fs::remove_all(test_dir); }

    const std::string test_dir = "acorn_index_reader_test";
    std::mt19937 _gen;

    std::vector<float> generate_random_vectors(int n, int dim) {
        std::vector<float> vecs(n * dim);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (auto& v : vecs) v = dist(_gen);
        return vecs;
    }

    void generate_geo_data(int n, double center_lat, double center_lng, double spread_km,
                           std::vector<double>& lats, std::vector<double>& lngs) {
        lats.resize(n);
        lngs.resize(n);
        std::uniform_real_distribution<double> dist(-spread_km, spread_km);
        for (int i = 0; i < n; i++) {
            double dlat = dist(_gen) / 111.0;
            double dlng = dist(_gen) / (111.0 * std::cos(center_lat * M_PI / 180.0));
            lats[i] = center_lat + dlat;
            lngs[i] = center_lng + dlng;
        }
    }

    std::string build_hnsw_index(int n, int M, int dim, const std::vector<float>& vectors,
                                 bool with_idmap = false) {
        std::string path = test_dir + "/acorn_test.vi";
        return test::build_hnsw_index_file(path, dim, M, n, vectors, with_idmap);
    }

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
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    ASSERT_EQ(reader.dimension(), kDim);
    ASSERT_EQ(reader.num_nodes(), kNumNodes);

    float query[kDim];
    std::copy(vectors.begin(), vectors.begin() + kDim, query);

    AcornIndexReader::SearchParams params;
    params.k = 5;
    params.ef_search = 40;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    ASSERT_GT(result.row_ids.size(), 0);
    ASSERT_LE(result.row_ids.size(), 5);
    ASSERT_EQ(result.row_ids[0], 0);
    ASSERT_FLOAT_EQ(result.distances[0], 0.0f);
}

TEST_F(AcornIndexReaderTest, test_search_with_idmap) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors, true);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    ASSERT_TRUE(reader.is_valid());
    ASSERT_EQ(reader.num_nodes(), kNumNodes);

    float query[kDim];
    std::copy(vectors.begin(), vectors.begin() + kDim, query);

    AcornIndexReader::SearchParams params;
    params.k = 5;
    params.ef_search = 40;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    ASSERT_GT(result.row_ids.size(), 0);
    ASSERT_EQ(result.row_ids[0], 0);
    ASSERT_FLOAT_EQ(result.distances[0], 0.0f);
}

TEST_F(AcornIndexReaderTest, test_search_with_radius_predicate) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors, true);

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
    params.ef_search = 100;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    SpatialRadiusEvaluator checker;
    ASSERT_OK(checker.init(spec, lats, lngs));
    for (auto id : result.row_ids) {
        ASSERT_TRUE(checker.evaluate(id)) << "Row " << id << " does not satisfy radius predicate";
    }
}

TEST_F(AcornIndexReaderTest, test_search_empty_predicate_match) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

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
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

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

TEST_F(AcornIndexReaderTest, test_search_with_polygon_predicate) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors, true);

    std::vector<double> lats(kNumNodes), lngs(kNumNodes);
    for (int i = 0; i < kNumNodes; i++) {
        if (i % 3 == 0) {
            lats[i] = 37.78;
            lngs[i] = -122.42;
        } else {
            lats[i] = 40.0;
            lngs[i] = -100.0;
        }
    }

    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    spec.wkt = "POLYGON((-122.5 37.7, -122.3 37.7, -122.3 37.85, -122.5 37.85, -122.5 37.7))";

    auto evaluator = create_predicate_evaluator(spec);
    ASSERT_NE(evaluator, nullptr);
    ASSERT_OK(evaluator->init(spec, lats, lngs));

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    reader.set_predicate_evaluator(std::move(evaluator));

    float query[kDim] = {0};
    AcornIndexReader::SearchParams params;
    params.k = 10;
    params.ef_search = 100;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    SpatialPolygonEvaluator checker;
    ASSERT_OK(checker.init(spec, lats, lngs));
    for (auto id : result.row_ids) {
        ASSERT_TRUE(checker.evaluate(id)) << "Row " << id << " is not inside polygon";
    }
}

TEST_F(AcornIndexReaderTest, test_search_varying_k) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

    float query[kDim];
    std::copy(vectors.begin(), vectors.begin() + kDim, query);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));

    for (int k : {1, 3, 5, 10, 20, 50}) {
        AcornIndexReader::SearchParams params;
        params.k = k;
        params.ef_search = std::max(40, k * 4);

        AcornIndexReader::SearchResult result;
        ASSERT_OK(reader.search(query, params, result));
        ASSERT_LE(static_cast<int>(result.row_ids.size()), k);
        ASSERT_GT(result.row_ids.size(), 0);
        ASSERT_EQ(result.row_ids[0], 0);
    }
}

TEST_F(AcornIndexReaderTest, test_recall_without_predicate) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));

    for (int qi = 0; qi < 5; qi++) {
        float query[kDim];
        std::copy(vectors.begin() + qi * kDim, vectors.begin() + (qi + 1) * kDim, query);

        AcornIndexReader::SearchParams params;
        params.k = 5;
        params.ef_search = 40;

        AcornIndexReader::SearchResult result;
        ASSERT_OK(reader.search(query, params, result));

        auto gt = brute_force_knn(vectors.data(), kNumNodes, kDim, query, 5);
        double recall = compute_recall(result.row_ids, gt);
        ASSERT_GE(recall, 0.2) << "Recall too low for query " << qi << ": " << recall;
        ASSERT_EQ(result.row_ids[0], qi) << "Nearest neighbor should be the query vector itself";
    }
}

TEST_F(AcornIndexReaderTest, test_distances_sorted) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

    float query[kDim] = {0};
    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));

    AcornIndexReader::SearchParams params;
    params.k = 20;
    params.ef_search = 80;

    AcornIndexReader::SearchResult result;
    ASSERT_OK(reader.search(query, params, result));

    ASSERT_EQ(result.row_ids.size(), result.distances.size());
    for (size_t i = 1; i < result.distances.size(); i++) {
        ASSERT_LE(result.distances[i - 1], result.distances[i])
                << "Distances not sorted at position " << i;
    }
}

TEST_F(AcornIndexReaderTest, test_init_invalid_path) {
    AcornIndexReader reader;
    auto st = reader.init("nonexistent_dir/nonexistent.vi");
    ASSERT_FALSE(st.ok());
    ASSERT_FALSE(reader.is_valid());
}

TEST_F(AcornIndexReaderTest, test_dimension_and_num_nodes) {
    auto vectors = generate_random_vectors(kNumNodes, kDim);
    auto path = build_hnsw_index(kNumNodes, kM, kDim, vectors);

    AcornIndexReader reader;
    ASSERT_OK(reader.init(path));
    ASSERT_TRUE(reader.is_valid());
    ASSERT_EQ(reader.dimension(), kDim);
    ASSERT_EQ(reader.num_nodes(), kNumNodes);
}

} // namespace starrocks

#endif
