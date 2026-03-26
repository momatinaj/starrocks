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

#include "storage/index/vector/hnsw_graph_accessor.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <fstream>
#include <vector>

#include "fs/fs.h"
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

    // Helper to write a minimal synthetic Faiss HNSW file for testing.
    // Creates a small graph with known structure.
    std::string write_synthetic_hnsw_file(int num_nodes, int M, int dim) {
        std::string path = test_dir + "/test_graph.vi";
        std::ofstream ofs(path, std::ios::binary);

        // FourCC: "IHNf" = 0x664E4849
        uint32_t fourcc = 0x664E4849;
        ofs.write(reinterpret_cast<const char*>(&fourcc), sizeof(fourcc));

        // Index header: d(int), ntotal(int64), dummy(int64), dummy(int64),
        //               is_trained(bool), metric_type(int)
        int d = dim;
        int64_t ntotal = num_nodes;
        int64_t dummy = 0;
        bool is_trained = true;
        int metric_type = 1; // L2

        ofs.write(reinterpret_cast<const char*>(&d), sizeof(d));
        ofs.write(reinterpret_cast<const char*>(&ntotal), sizeof(ntotal));
        ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
        ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
        ofs.write(reinterpret_cast<const char*>(&is_trained), sizeof(is_trained));
        ofs.write(reinterpret_cast<const char*>(&metric_type), sizeof(metric_type));

        // HNSW graph:
        // assign_probas (vector<double>)
        std::vector<double> assign_probas = {0.5, 0.25};
        uint64_t size = assign_probas.size();
        ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
        ofs.write(reinterpret_cast<const char*>(assign_probas.data()), size * sizeof(double));

        // cum_nneighbor_per_level (vector<int>): level 0 has 2*M slots, level 1 has +M
        int slots_per_level0 = 2 * M;
        int slots_per_level1 = M;
        std::vector<int> cum_nn = {slots_per_level0, slots_per_level0 + slots_per_level1};
        size = cum_nn.size();
        ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
        ofs.write(reinterpret_cast<const char*>(cum_nn.data()), size * sizeof(int));

        // levels (vector<int>): node 0 has level 2, others have level 1
        std::vector<int> levels(num_nodes, 1);
        levels[0] = 2; // entry point has higher level
        size = levels.size();
        ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
        ofs.write(reinterpret_cast<const char*>(levels.data()), size * sizeof(int));

        // Build offsets and neighbors
        int total_slots_per_node_base = slots_per_level0; // nodes at level 1 only use level 0 slots
        int total_slots_node0 = slots_per_level0 + slots_per_level1; // node 0 has 2 levels

        std::vector<size_t> offsets;
        std::vector<int32_t> neighbors;

        size_t current_offset = 0;
        for (int i = 0; i < num_nodes; i++) {
            offsets.push_back(current_offset);
            int node_total_slots = (levels[i] == 2) ? total_slots_node0 : total_slots_per_node_base;

            // Level 0 neighbors: connect to adjacent nodes (ring topology)
            for (int j = 0; j < slots_per_level0; j++) {
                if (j < 2 && num_nodes > 1) {
                    int neighbor = (j == 0) ? ((i + 1) % num_nodes) : ((i + num_nodes - 1) % num_nodes);
                    neighbors.push_back(neighbor);
                } else {
                    neighbors.push_back(-1); // unused slot
                }
            }

            // Level 1 neighbors (only for node 0)
            if (levels[i] == 2) {
                for (int j = 0; j < slots_per_level1; j++) {
                    neighbors.push_back(-1); // no level-1 connections in this simple test
                }
            }

            current_offset += node_total_slots;
        }
        offsets.push_back(current_offset);

        // Write offsets
        size = offsets.size();
        ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
        ofs.write(reinterpret_cast<const char*>(offsets.data()), size * sizeof(size_t));

        // Write neighbors
        size = neighbors.size();
        ofs.write(reinterpret_cast<const char*>(&size), sizeof(size));
        ofs.write(reinterpret_cast<const char*>(neighbors.data()), size * sizeof(int32_t));

        // entry_point, max_level, efConstruction, efSearch, upper_beam
        int32_t entry_point = 0;
        int max_level = 1; // 0-indexed: max level is 1 (2 levels: 0 and 1)
        int efConstruction = 40;
        int efSearch = 16;
        int upper_beam = 1;
        ofs.write(reinterpret_cast<const char*>(&entry_point), sizeof(entry_point));
        ofs.write(reinterpret_cast<const char*>(&max_level), sizeof(max_level));
        ofs.write(reinterpret_cast<const char*>(&efConstruction), sizeof(efConstruction));
        ofs.write(reinterpret_cast<const char*>(&efSearch), sizeof(efSearch));
        ofs.write(reinterpret_cast<const char*>(&upper_beam), sizeof(upper_beam));

        ofs.close();
        return path;
    }
};

TEST_F(HNSWGraphAccessorTest, test_load_synthetic_graph) {
    auto path = write_synthetic_hnsw_file(10, 4, 16);

    HNSWGraphAccessor accessor;
    auto status = accessor.init(path);
    ASSERT_TRUE(status.ok()) << status.message();
    ASSERT_TRUE(accessor.is_valid());
    ASSERT_EQ(accessor.num_nodes(), 10);
    ASSERT_EQ(accessor.entry_point(), 0);
    ASSERT_EQ(accessor.M(), 4);
}

TEST_F(HNSWGraphAccessorTest, test_neighbors_ring_topology) {
    auto path = write_synthetic_hnsw_file(5, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    // Node 1's level-0 neighbors should be 2 and 0 (ring topology)
    auto nbrs = accessor.neighbors(1, 0);
    ASSERT_GE(nbrs.size(), 2);
    std::unordered_set<int32_t> nbr_set(nbrs.begin(), nbrs.end());
    ASSERT_TRUE(nbr_set.count(2) > 0);
    ASSERT_TRUE(nbr_set.count(0) > 0);
}

TEST_F(HNSWGraphAccessorTest, test_node_levels) {
    auto path = write_synthetic_hnsw_file(5, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    // Node 0 has level 2 (levels[0]=2), others have level 1
    ASSERT_EQ(accessor.node_level(0), 2);
    ASSERT_EQ(accessor.node_level(1), 1);
    ASSERT_EQ(accessor.node_level(4), 1);
}

TEST_F(HNSWGraphAccessorTest, test_empty_neighbors_above_level) {
    auto path = write_synthetic_hnsw_file(5, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    // Node 1 is at level 1, so requesting level 1 neighbors should be empty
    auto nbrs = accessor.neighbors(1, 1);
    ASSERT_TRUE(nbrs.empty());
}

TEST_F(HNSWGraphAccessorTest, test_expanded_neighbors) {
    auto path = write_synthetic_hnsw_file(5, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    // 2-hop expansion of node 0 at level 0:
    // 1-hop: {1, 4} (ring neighbors)
    // 2-hop from 1: {2, 0} → adds 2
    // 2-hop from 4: {0, 3} → adds 3
    // Expanded (excluding self): {1, 4, 2, 3}
    auto expanded = accessor.expanded_neighbors(0, 0);
    ASSERT_GE(expanded.size(), 3); // at least 3 unique neighbors in 2-hop
    std::unordered_set<int32_t> exp_set(expanded.begin(), expanded.end());
    ASSERT_TRUE(exp_set.count(0) == 0); // self excluded
    ASSERT_TRUE(exp_set.count(1) > 0);
    ASSERT_TRUE(exp_set.count(4) > 0);
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
    auto path = write_synthetic_hnsw_file(5, 4, 3);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    auto nbrs = accessor.neighbors(100, 0);
    ASSERT_TRUE(nbrs.empty());

    auto nbrs2 = accessor.neighbors(-1, 0);
    ASSERT_TRUE(nbrs2.empty());
}

TEST_F(HNSWGraphAccessorTest, test_dimension_and_ntotal) {
    auto path = write_synthetic_hnsw_file(10, 4, 16);

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path));

    ASSERT_EQ(accessor.dimension(), 16);
    ASSERT_EQ(accessor.ntotal(), 10);
    ASSERT_FALSE(accessor.has_stored_vectors());
}

TEST_F(HNSWGraphAccessorTest, test_load_with_vectors) {
    const int num_nodes = 5;
    const int dim = 3;
    const int M = 4;

    std::string path = test_dir + "/test_with_vectors.vi";
    std::ofstream ofs(path, std::ios::binary);

    // Write IndexHNSWFlat header
    uint32_t fourcc = 0x664E4849; // "IHNf"
    ofs.write(reinterpret_cast<const char*>(&fourcc), sizeof(fourcc));

    int d = dim;
    int64_t ntotal = num_nodes;
    int64_t dummy = 0;
    bool is_trained = true;
    int metric_type = 1;
    ofs.write(reinterpret_cast<const char*>(&d), sizeof(d));
    ofs.write(reinterpret_cast<const char*>(&ntotal), sizeof(ntotal));
    ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
    ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
    ofs.write(reinterpret_cast<const char*>(&is_trained), sizeof(is_trained));
    ofs.write(reinterpret_cast<const char*>(&metric_type), sizeof(metric_type));

    // HNSW graph section (minimal)
    std::vector<double> assign_probas = {0.5, 0.25};
    uint64_t sz = assign_probas.size();
    ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    ofs.write(reinterpret_cast<const char*>(assign_probas.data()), sz * sizeof(double));

    std::vector<int> cum_nn = {2 * M, 2 * M + M};
    sz = cum_nn.size();
    ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    ofs.write(reinterpret_cast<const char*>(cum_nn.data()), sz * sizeof(int));

    std::vector<int> levels(num_nodes, 1);
    levels[0] = 2;
    sz = levels.size();
    ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    ofs.write(reinterpret_cast<const char*>(levels.data()), sz * sizeof(int));

    std::vector<size_t> offsets;
    std::vector<int32_t> neighbors;
    size_t off = 0;
    for (int i = 0; i < num_nodes; i++) {
        offsets.push_back(off);
        int slots = (levels[i] == 2) ? (2 * M + M) : 2 * M;
        for (int j = 0; j < slots; j++) {
            neighbors.push_back((j < 2 && num_nodes > 1) ?
                ((j == 0) ? (i + 1) % num_nodes : (i + num_nodes - 1) % num_nodes) : -1);
        }
        off += slots;
    }
    offsets.push_back(off);

    sz = offsets.size();
    ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    ofs.write(reinterpret_cast<const char*>(offsets.data()), sz * sizeof(size_t));

    sz = neighbors.size();
    ofs.write(reinterpret_cast<const char*>(&sz), sizeof(sz));
    ofs.write(reinterpret_cast<const char*>(neighbors.data()), sz * sizeof(int32_t));

    int32_t entry = 0;
    int max_level = 1, efc = 40, efs = 16, ub = 1;
    ofs.write(reinterpret_cast<const char*>(&entry), sizeof(entry));
    ofs.write(reinterpret_cast<const char*>(&max_level), sizeof(max_level));
    ofs.write(reinterpret_cast<const char*>(&efc), sizeof(efc));
    ofs.write(reinterpret_cast<const char*>(&efs), sizeof(efs));
    ofs.write(reinterpret_cast<const char*>(&ub), sizeof(ub));

    // Write IndexFlat storage section
    uint32_t storage_fourcc = 0x6C467849; // "IxFl"
    ofs.write(reinterpret_cast<const char*>(&storage_fourcc), sizeof(storage_fourcc));

    ofs.write(reinterpret_cast<const char*>(&d), sizeof(d));
    ofs.write(reinterpret_cast<const char*>(&ntotal), sizeof(ntotal));
    ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
    ofs.write(reinterpret_cast<const char*>(&dummy), sizeof(dummy));
    ofs.write(reinterpret_cast<const char*>(&is_trained), sizeof(is_trained));
    ofs.write(reinterpret_cast<const char*>(&metric_type), sizeof(metric_type));

    std::vector<float> vecs = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
    uint64_t byte_count = static_cast<uint64_t>(vecs.size()) * sizeof(float);
    ofs.write(reinterpret_cast<const char*>(&byte_count), sizeof(byte_count));
    ofs.write(reinterpret_cast<const char*>(vecs.data()), byte_count);

    ofs.close();

    HNSWGraphAccessor accessor;
    ASSERT_OK(accessor.init(path, true));
    ASSERT_TRUE(accessor.is_valid());
    ASSERT_TRUE(accessor.has_stored_vectors());
    ASSERT_EQ(accessor.dimension(), dim);
    ASSERT_EQ(accessor.ntotal(), num_nodes);

    const float* stored = accessor.stored_vectors();
    ASSERT_NE(stored, nullptr);
    ASSERT_FLOAT_EQ(stored[0], 1.0f);
    ASSERT_FLOAT_EQ(stored[3], 4.0f);
    ASSERT_FLOAT_EQ(stored[14], 15.0f);
}

} // namespace starrocks
