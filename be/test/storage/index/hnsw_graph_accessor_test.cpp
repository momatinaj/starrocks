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

#include "storage/index/vector/hnsw_graph_accessor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <random>
#include <unordered_set>
#include <vector>

#include "faiss/IndexFlat.h"
#include "faiss/IndexHNSW.h"
#include "faiss/IndexIDMap.h"
#include "faiss/index_io.h"
#include "fs/fs.h"
#include "fs/fs_util.h"
#include "testutil/assert.h"

namespace starrocks {

class HNSWGraphAccessorTest : public testing::Test {
public:
    HNSWGraphAccessorTest() = default;

protected:
    void SetUp() override {
        CHECK_OK(fs::remove_all(test_dir));
        CHECK_OK(fs::create_directories(test_dir));
    }

    void TearDown() override { fs::remove_all(test_dir); }

    const std::string test_dir = "hnsw_graph_accessor_test";

    // Build an IndexHNSWFlat with random data and write it using Faiss's API.
    std::string write_hnsw_flat(int num_nodes, int M, int dim, bool wrap_idmap = false) {
        std::string path = test_dir + "/test_graph.vi";

        auto* hnsw_index = new faiss::IndexHNSWFlat(dim, M);
        hnsw_index->hnsw.efConstruction = 40;
        hnsw_index->hnsw.efSearch = 16;

        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> vectors(num_nodes * dim);
        for (auto& v : vectors) v = dist(rng);

        if (wrap_idmap) {
            faiss::IndexIDMap id_map_index(hnsw_index);
            std::vector<int64_t> ids(num_nodes);
            for (int i = 0; i < num_nodes; i++) ids[i] = i * 2; // external IDs: 0, 2, 4, 6, ...
            id_map_index.add_with_ids(num_nodes, vectors.data(), ids.data());
            faiss::write_index(&id_map_index, path.c_str());
            // IndexIDMap does NOT own the sub-index when constructed this way,
            // so we must delete hnsw_index manually after write.
            delete hnsw_index;
        } else {
            hnsw_index->add(num_nodes, vectors.data());
            faiss::write_index(hnsw_index, path.c_str());
            delete hnsw_index;
        }

        return path;
    }

    // Generate random vectors
    std::vector<float> generate_random_vectors(int n, int dim) {
        std::mt19937 rng(42);
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        std::vector<float> vecs(n * dim);
        for (auto& v : vecs) v = dist(rng);
        return vecs;
    }
};

TEST_F(HNSWGraphAccessorTest, test_load_faiss_hnsw_flat) {
    auto path = write_hnsw_flat(100, 16, 8);

    HNSWGraphAccessor accessor;
    auto status = accessor.init(path);
    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_TRUE(accessor.is_valid());
    ASSERT_EQ(accessor.num_nodes(), 100);
    ASSERT_EQ(accessor.dimension(), 8);
    ASSERT_EQ(accessor.ntotal(), 100);
    ASSERT_GE(accessor.entry_point(), 0);
    ASSERT_EQ(accessor.M(), 16);
    ASSERT_FALSE(accessor.has_id_map());
}

TEST_F(HNSWGraphAccessorTest, test_load_faiss_hnsw_with_idmap) {
    auto path = write_hnsw_flat(50, 8, 4, true);

    HNSWGraphAccessor accessor;
    auto status = accessor.init(path);
    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_TRUE(accessor.is_valid());
    ASSERT_EQ(accessor.num_nodes(), 50);
    ASSERT_TRUE(accessor.has_id_map());

    // External IDs should be 0, 2, 4, 6, ...
    ASSERT_EQ(accessor.map_to_external_id(0), 0);
    ASSERT_EQ(accessor.map_to_external_id(1), 2);
    ASSERT_EQ(accessor.map_to_external_id(2), 4);
    ASSERT_EQ(accessor.map_to_external_id(49), 98);
}

TEST_F(HNSWGraphAccessorTest, test_neighbors) {
    auto path = write_hnsw_flat(50, 8, 4);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    auto nbrs = accessor.neighbors(0, 0);
    ASSERT_GT(nbrs.size(), 0);
    for (auto n : nbrs) {
        ASSERT_GE(n, 0);
        ASSERT_LT(n, 50);
    }
}

TEST_F(HNSWGraphAccessorTest, test_expanded_neighbors_self_exclusion) {
    auto path = write_hnsw_flat(50, 8, 4);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    for (int node = 0; node < 10; node++) {
        auto expanded = accessor.expanded_neighbors(node, 0);
        for (auto n : expanded) {
            ASSERT_NE(n, node) << "expanded_neighbors should not contain self (node " << node << ")";
        }
    }
}

TEST_F(HNSWGraphAccessorTest, test_load_with_vectors) {
    auto path = write_hnsw_flat(20, 8, 4);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path, true));
    ASSERT_TRUE(accessor.is_valid());
    ASSERT_TRUE(accessor.has_stored_vectors());
    ASSERT_EQ(accessor.dimension(), 4);
    ASSERT_EQ(accessor.ntotal(), 20);

    const float* stored = accessor.stored_vectors();
    ASSERT_NE(stored, nullptr);
}

TEST_F(HNSWGraphAccessorTest, test_load_without_vectors) {
    auto path = write_hnsw_flat(20, 8, 4);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path, false));
    ASSERT_TRUE(accessor.is_valid());
    ASSERT_FALSE(accessor.has_stored_vectors());
}

TEST_F(HNSWGraphAccessorTest, test_invalid_file) {
    std::string path = test_dir + "/invalid.vi";
    std::ofstream ofs(path, std::ios::binary);
    uint32_t bad_fourcc = 0xDEADBEEF;
    ofs.write(reinterpret_cast<const char*>(&bad_fourcc), sizeof(bad_fourcc));
    ofs.close();

    HNSWGraphAccessor accessor;
    auto status = accessor.init(path);
    ASSERT_FALSE(status.ok());
    ASSERT_FALSE(accessor.is_valid());
}

TEST_F(HNSWGraphAccessorTest, test_missing_file) {
    HNSWGraphAccessor accessor;
    auto status = accessor.init(test_dir + "/nonexistent.vi");
    ASSERT_FALSE(status.ok());
}

TEST_F(HNSWGraphAccessorTest, test_out_of_range_node) {
    auto path = write_hnsw_flat(10, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    auto nbrs = accessor.neighbors(100, 0);
    ASSERT_TRUE(nbrs.empty());

    auto nbrs2 = accessor.neighbors(-1, 0);
    ASSERT_TRUE(nbrs2.empty());
}

TEST_F(HNSWGraphAccessorTest, test_map_to_external_id_no_map) {
    auto path = write_hnsw_flat(10, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));
    ASSERT_FALSE(accessor.has_id_map());

    // Without IDMap, internal ID = external ID
    ASSERT_EQ(accessor.map_to_external_id(0), 0);
    ASSERT_EQ(accessor.map_to_external_id(5), 5);
    ASSERT_EQ(accessor.map_to_external_id(-1), -1);
}

TEST_F(HNSWGraphAccessorTest, test_node_levels) {
    auto path = write_hnsw_flat(100, 16, 4);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    // Entry point should have the highest level
    int entry_level = accessor.node_level(accessor.entry_point());
    ASSERT_GE(entry_level, 1);

    // All nodes should have at least level 1
    for (int i = 0; i < accessor.num_nodes(); i++) {
        ASSERT_GE(accessor.node_level(i), 1);
    }
}

TEST_F(HNSWGraphAccessorTest, test_empty_file) {
    std::string path = test_dir + "/empty.vi";
    std::ofstream ofs(path, std::ios::binary);
    ofs.close();

    HNSWGraphAccessor accessor;
    auto status = accessor.init(path);
    ASSERT_FALSE(status.ok());
}

} // namespace starrocks

#endif
