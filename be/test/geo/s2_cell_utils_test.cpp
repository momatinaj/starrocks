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

#include "geo/s2_cell_utils.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <unordered_set>

namespace starrocks {

class S2CellUtilsTest : public testing::Test {};

TEST_F(S2CellUtilsTest, cell_id_from_latlng_basic) {
    // Beijing: 39.9N, 116.3E at level 12
    uint64_t cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    ASSERT_NE(cell, 0);

    // Same point at same level should produce the same cell
    uint64_t cell2 = s2_cell_id_from_latlng(39.9, 116.3, 12);
    ASSERT_EQ(cell, cell2);
}

TEST_F(S2CellUtilsTest, nearby_points_same_cell) {
    // Two points ~100m apart should be in the same level-12 cell (~3.3 km²)
    uint64_t cell1 = s2_cell_id_from_latlng(39.9000, 116.3000, 12);
    uint64_t cell2 = s2_cell_id_from_latlng(39.9005, 116.3005, 12);
    ASSERT_EQ(cell1, cell2);
}

TEST_F(S2CellUtilsTest, distant_points_different_cells) {
    // Beijing vs Shanghai should be in different cells at any reasonable level
    uint64_t beijing = s2_cell_id_from_latlng(39.9, 116.3, 12);
    uint64_t shanghai = s2_cell_id_from_latlng(31.2, 121.5, 12);
    ASSERT_NE(beijing, shanghai);
}

TEST_F(S2CellUtilsTest, different_levels_different_cells) {
    // Same point at different levels should produce different cell IDs
    uint64_t level10 = s2_cell_id_from_latlng(39.9, 116.3, 10);
    uint64_t level12 = s2_cell_id_from_latlng(39.9, 116.3, 12);
    uint64_t level14 = s2_cell_id_from_latlng(39.9, 116.3, 14);
    ASSERT_NE(level10, level12);
    ASSERT_NE(level12, level14);
}

TEST_F(S2CellUtilsTest, invalid_coordinates_return_zero) {
    ASSERT_EQ(s2_cell_id_from_latlng(91, 0, 12), 0);
    ASSERT_EQ(s2_cell_id_from_latlng(-91, 0, 12), 0);
    ASSERT_EQ(s2_cell_id_from_latlng(0, 181, 12), 0);
    ASSERT_EQ(s2_cell_id_from_latlng(0, -181, 12), 0);
}

TEST_F(S2CellUtilsTest, invalid_level_returns_zero) {
    ASSERT_EQ(s2_cell_id_from_latlng(39.9, 116.3, -1), 0);
    ASSERT_EQ(s2_cell_id_from_latlng(39.9, 116.3, 31), 0);
}

TEST_F(S2CellUtilsTest, boundary_coordinates) {
    ASSERT_NE(s2_cell_id_from_latlng(90, 180, 12), 0);
    ASSERT_NE(s2_cell_id_from_latlng(-90, -180, 12), 0);
    ASSERT_NE(s2_cell_id_from_latlng(0, 0, 12), 0);
}

TEST_F(S2CellUtilsTest, token_roundtrip) {
    uint64_t cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    std::string token = s2_cell_id_to_token(cell);
    ASSERT_FALSE(token.empty());

    uint64_t recovered = s2_cell_id_from_token(token);
    ASSERT_EQ(cell, recovered);
}

TEST_F(S2CellUtilsTest, invalid_token_returns_zero) {
    ASSERT_EQ(s2_cell_id_from_token(""), 0);
    ASSERT_EQ(s2_cell_id_from_token("not_a_valid_token_xyz"), 0);
}

TEST_F(S2CellUtilsTest, covering_contains_point_cell) {
    // A 5km circle around Beijing should produce a covering that includes
    // the cell containing the center point
    uint64_t center_cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    auto covering = s2_covering_cell_ids_for_cap(39.9, 116.3, 5000, 12);

    ASSERT_FALSE(covering.empty());
    ASSERT_NE(std::find(covering.begin(), covering.end(), center_cell), covering.end());
}

TEST_F(S2CellUtilsTest, covering_size_increases_with_radius) {
    auto small = s2_covering_cell_ids_for_cap(39.9, 116.3, 1000, 12);
    auto large = s2_covering_cell_ids_for_cap(39.9, 116.3, 50000, 12, 64);

    ASSERT_LE(small.size(), large.size());
}

TEST_F(S2CellUtilsTest, covering_invalid_inputs) {
    ASSERT_TRUE(s2_covering_cell_ids_for_cap(91, 0, 1000, 12).empty());
    ASSERT_TRUE(s2_covering_cell_ids_for_cap(39.9, 116.3, -1, 12).empty());
    ASSERT_TRUE(s2_covering_cell_ids_for_cap(39.9, 116.3, 1000, 31).empty());
}

TEST_F(S2CellUtilsTest, covering_no_duplicates) {
    auto covering = s2_covering_cell_ids_for_cap(39.9, 116.3, 10000, 12, 32);
    std::unordered_set<uint64_t> unique(covering.begin(), covering.end());
    ASSERT_EQ(unique.size(), covering.size());
}

TEST_F(S2CellUtilsTest, level_validation) {
    ASSERT_TRUE(s2_is_valid_level(0));
    ASSERT_TRUE(s2_is_valid_level(12));
    ASSERT_TRUE(s2_is_valid_level(30));
    ASSERT_FALSE(s2_is_valid_level(-1));
    ASSERT_FALSE(s2_is_valid_level(31));
}

TEST_F(S2CellUtilsTest, all_levels_produce_valid_cells) {
    for (int level = 0; level <= 30; level++) {
        uint64_t cell = s2_cell_id_from_latlng(39.9, 116.3, level);
        ASSERT_NE(cell, 0) << "Failed at level " << level;
        std::string token = s2_cell_id_to_token(cell);
        ASSERT_FALSE(token.empty()) << "Empty token at level " << level;
    }
}

} // namespace starrocks
