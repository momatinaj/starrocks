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
#include <s2/s2cap.h>
#include <s2/s2earth.h>
#include <s2/s2latlng.h>
#include <s2/s2loop.h>
#include <s2/s2polygon.h>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace starrocks {

class S2CellUtilsTest : public testing::Test {};

// ==================== Cell ID assignment ====================

TEST_F(S2CellUtilsTest, cell_id_deterministic) {
    uint64_t cell1 = s2_cell_id_from_latlng(39.9, 116.3, 12);
    uint64_t cell2 = s2_cell_id_from_latlng(39.9, 116.3, 12);
    ASSERT_NE(cell1, 0);
    ASSERT_EQ(cell1, cell2);
}

TEST_F(S2CellUtilsTest, nearby_points_same_cell) {
    uint64_t cell1 = s2_cell_id_from_latlng(39.9000, 116.3000, 12);
    uint64_t cell2 = s2_cell_id_from_latlng(39.9005, 116.3005, 12);
    ASSERT_EQ(cell1, cell2);
}

TEST_F(S2CellUtilsTest, distant_points_different_cells) {
    uint64_t beijing = s2_cell_id_from_latlng(39.9, 116.3, 12);
    uint64_t shanghai = s2_cell_id_from_latlng(31.2, 121.5, 12);
    ASSERT_NE(beijing, shanghai);
}

TEST_F(S2CellUtilsTest, different_levels_different_cells) {
    uint64_t level10 = s2_cell_id_from_latlng(39.9, 116.3, 10);
    uint64_t level12 = s2_cell_id_from_latlng(39.9, 116.3, 12);
    uint64_t level14 = s2_cell_id_from_latlng(39.9, 116.3, 14);
    ASSERT_NE(level10, level12);
    ASSERT_NE(level12, level14);
}

TEST_F(S2CellUtilsTest, cell_hierarchy_consistency) {
    // A finer-level cell's parent at a coarser level must match
    // the cell computed directly at that coarser level
    uint64_t coarse = s2_cell_id_from_latlng(39.9, 116.3, 10);
    uint64_t fine = s2_cell_id_from_latlng(39.9, 116.3, 14);
    S2CellId fine_cell(fine);
    ASSERT_EQ(fine_cell.parent(10).id(), coarse);
}

TEST_F(S2CellUtilsTest, multiple_hemispheres) {
    // Northern + Eastern (Beijing)
    ASSERT_NE(s2_cell_id_from_latlng(39.9, 116.3, 12), 0);
    // Southern + Eastern (Sydney)
    ASSERT_NE(s2_cell_id_from_latlng(-33.8, 151.2, 12), 0);
    // Northern + Western (New York)
    ASSERT_NE(s2_cell_id_from_latlng(40.7, -74.0, 12), 0);
    // Southern + Western (São Paulo)
    ASSERT_NE(s2_cell_id_from_latlng(-23.5, -46.6, 12), 0);

    // All four should be distinct cells
    std::unordered_set<uint64_t> cells;
    cells.insert(s2_cell_id_from_latlng(39.9, 116.3, 12));
    cells.insert(s2_cell_id_from_latlng(-33.8, 151.2, 12));
    cells.insert(s2_cell_id_from_latlng(40.7, -74.0, 12));
    cells.insert(s2_cell_id_from_latlng(-23.5, -46.6, 12));
    ASSERT_EQ(cells.size(), 4);
}

TEST_F(S2CellUtilsTest, antimeridian_points) {
    // Points just east and west of the antimeridian (180° longitude)
    uint64_t east = s2_cell_id_from_latlng(0, 179.99, 12);
    uint64_t west = s2_cell_id_from_latlng(0, -179.99, 12);
    ASSERT_NE(east, 0);
    ASSERT_NE(west, 0);
    ASSERT_NE(east, west);
}

TEST_F(S2CellUtilsTest, poles) {
    uint64_t north_pole = s2_cell_id_from_latlng(90, 0, 12);
    uint64_t south_pole = s2_cell_id_from_latlng(-90, 0, 12);
    ASSERT_NE(north_pole, 0);
    ASSERT_NE(south_pole, 0);
    ASSERT_NE(north_pole, south_pole);

    // North pole at any longitude should be the same cell (pole is a single point)
    uint64_t north_pole2 = s2_cell_id_from_latlng(90, 123.4, 12);
    ASSERT_EQ(north_pole, north_pole2);
}

// ==================== Input validation ====================

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

// ==================== Token serialization ====================

TEST_F(S2CellUtilsTest, token_roundtrip) {
    uint64_t cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    std::string token = s2_cell_id_to_token(cell);
    ASSERT_FALSE(token.empty());
    uint64_t recovered = s2_cell_id_from_token(token);
    ASSERT_EQ(cell, recovered);
}

TEST_F(S2CellUtilsTest, token_roundtrip_all_hemispheres) {
    double coords[][2] = {{39.9, 116.3}, {-33.8, 151.2}, {40.7, -74.0}, {-23.5, -46.6}, {0, 0}};
    for (auto& c : coords) {
        uint64_t cell = s2_cell_id_from_latlng(c[0], c[1], 14);
        std::string token = s2_cell_id_to_token(cell);
        ASSERT_EQ(s2_cell_id_from_token(token), cell)
                << "Roundtrip failed for lat=" << c[0] << " lng=" << c[1];
    }
}

TEST_F(S2CellUtilsTest, token_uniqueness) {
    // Different cells must produce different tokens
    std::string t1 = s2_cell_id_to_token(s2_cell_id_from_latlng(39.9, 116.3, 12));
    std::string t2 = s2_cell_id_to_token(s2_cell_id_from_latlng(31.2, 121.5, 12));
    ASSERT_NE(t1, t2);
}

TEST_F(S2CellUtilsTest, invalid_token_returns_zero) {
    ASSERT_EQ(s2_cell_id_from_token(""), 0);
    ASSERT_EQ(s2_cell_id_from_token("not_a_valid_token_xyz"), 0);
}

TEST_F(S2CellUtilsTest, token_is_filesystem_safe) {
    // Tokens should only contain hex chars — safe for file naming
    uint64_t cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    std::string token = s2_cell_id_to_token(cell);
    for (char c : token) {
        ASSERT_TRUE((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))
                << "Non-hex character '" << c << "' in token: " << token;
    }
}

// ==================== Region covering ====================

TEST_F(S2CellUtilsTest, covering_contains_center_cell) {
    uint64_t center_cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    auto covering = s2_covering_cell_ids_for_cap(39.9, 116.3, 5000, 12);
    ASSERT_FALSE(covering.empty());
    ASSERT_NE(std::find(covering.begin(), covering.end(), center_cell), covering.end());
}

TEST_F(S2CellUtilsTest, covering_completeness) {
    // Every point inside the cap should map to a cell in the covering.
    // Test with a grid of points inside a 2km circle.
    double center_lat = 39.9, center_lng = 116.3;
    double radius_m = 2000;
    int level = 12;
    auto covering = s2_covering_cell_ids_for_cap(center_lat, center_lng, radius_m, level);
    std::unordered_set<uint64_t> cover_set(covering.begin(), covering.end());

    // Sample points in a grid within ~1.5km of center (well inside the 2km cap)
    double delta_deg = 0.01; // ~1.1km at this latitude
    for (double dlat = -delta_deg; dlat <= delta_deg; dlat += delta_deg / 3) {
        for (double dlng = -delta_deg; dlng <= delta_deg; dlng += delta_deg / 3) {
            double lat = center_lat + dlat;
            double lng = center_lng + dlng;
            uint64_t cell = s2_cell_id_from_latlng(lat, lng, level);
            ASSERT_TRUE(cover_set.count(cell) > 0)
                    << "Point (" << lat << ", " << lng << ") cell not in covering";
        }
    }
}

TEST_F(S2CellUtilsTest, covering_size_increases_with_radius) {
    auto small = s2_covering_cell_ids_for_cap(39.9, 116.3, 1000, 12);
    auto large = s2_covering_cell_ids_for_cap(39.9, 116.3, 50000, 12, 64);
    ASSERT_LE(small.size(), large.size());
}

TEST_F(S2CellUtilsTest, covering_no_duplicates) {
    auto covering = s2_covering_cell_ids_for_cap(39.9, 116.3, 10000, 12, 32);
    std::unordered_set<uint64_t> unique(covering.begin(), covering.end());
    ASSERT_EQ(unique.size(), covering.size());
}

TEST_F(S2CellUtilsTest, covering_zero_radius) {
    // A zero-radius cap should cover exactly 1 cell (the point itself)
    auto covering = s2_covering_cell_ids_for_cap(39.9, 116.3, 0, 12);
    ASSERT_EQ(covering.size(), 1);
    ASSERT_EQ(covering[0], s2_cell_id_from_latlng(39.9, 116.3, 12));
}

TEST_F(S2CellUtilsTest, covering_very_large_radius) {
    // A hemisphere-sized cap (~10000km) should still produce a valid covering
    auto covering = s2_covering_cell_ids_for_cap(0, 0, 10000000, 4, 128);
    ASSERT_FALSE(covering.empty());
    // At level 4 there are 6*4^4 = 1536 cells total; a hemisphere covers ~half
    ASSERT_GT(covering.size(), 100);
}

TEST_F(S2CellUtilsTest, covering_invalid_inputs) {
    ASSERT_TRUE(s2_covering_cell_ids_for_cap(91, 0, 1000, 12).empty());
    ASSERT_TRUE(s2_covering_cell_ids_for_cap(39.9, 116.3, -1, 12).empty());
    ASSERT_TRUE(s2_covering_cell_ids_for_cap(39.9, 116.3, 1000, 31).empty());
}

TEST_F(S2CellUtilsTest, covering_antimeridian_cap) {
    // A cap centered near the antimeridian should produce cells on both sides
    auto covering = s2_covering_cell_ids_for_cap(0, 179.9, 50000, 12, 32);
    ASSERT_FALSE(covering.empty());
    // Verify the center point's cell is included
    uint64_t center_cell = s2_cell_id_from_latlng(0, 179.9, 12);
    ASSERT_NE(std::find(covering.begin(), covering.end(), center_cell), covering.end());
}

TEST_F(S2CellUtilsTest, covering_general_s2region_matches_cap) {
    // The general s2_covering_cell_ids(S2Region&) should produce the same
    // result as s2_covering_cell_ids_for_cap for the same geometric cap
    double lat = 39.9, lng = 116.3, radius_m = 5000;
    int level = 12;

    S2LatLng center = S2LatLng::FromDegrees(lat, lng);
    S1Angle radius = S2Earth::ToAngle(util::units::Meters(radius_m));
    S2Cap cap(center.ToPoint(), radius);

    auto from_cap_fn = s2_covering_cell_ids_for_cap(lat, lng, radius_m, level);
    auto from_region = s2_covering_cell_ids(cap, level);

    std::set<uint64_t> set1(from_cap_fn.begin(), from_cap_fn.end());
    std::set<uint64_t> set2(from_region.begin(), from_region.end());
    ASSERT_EQ(set1, set2);
}

TEST_F(S2CellUtilsTest, covering_polygon_region) {
    std::vector<S2Point> pts;
    pts.push_back(S2LatLng::FromDegrees(39.8, 116.2).ToPoint());
    pts.push_back(S2LatLng::FromDegrees(39.8, 116.4).ToPoint());
    pts.push_back(S2LatLng::FromDegrees(40.0, 116.4).ToPoint());
    pts.push_back(S2LatLng::FromDegrees(40.0, 116.2).ToPoint());
    auto loop = std::make_unique<S2Loop>(pts);
    loop->Normalize();
    S2Polygon polygon(std::move(loop));

    auto covering = s2_covering_cell_ids(polygon, 12, 64);
    ASSERT_FALSE(covering.empty());

    uint64_t center_cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    ASSERT_NE(std::find(covering.begin(), covering.end(), center_cell), covering.end());
}

TEST_F(S2CellUtilsTest, covering_polygon_wkt) {
    std::string wkt = "POLYGON((116.2 39.8, 116.4 39.8, 116.4 40.0, 116.2 40.0, 116.2 39.8))";
    auto covering = s2_covering_cell_ids_for_polygon_wkt(wkt, 12, 64);
    ASSERT_FALSE(covering.empty());

    uint64_t center_cell = s2_cell_id_from_latlng(39.9, 116.3, 12);
    ASSERT_NE(std::find(covering.begin(), covering.end(), center_cell), covering.end());
}

TEST_F(S2CellUtilsTest, covering_polygon_wkt_invalid) {
    ASSERT_TRUE(s2_covering_cell_ids_for_polygon_wkt("", 12).empty());
    ASSERT_TRUE(s2_covering_cell_ids_for_polygon_wkt("not a polygon", 12).empty());
    ASSERT_TRUE(s2_covering_cell_ids_for_polygon_wkt("POINT(116.3 39.9)", 12).empty());
}

// ==================== Partition distribution ====================

TEST_F(S2CellUtilsTest, uniform_grid_distributes_across_cells) {
    // A uniform grid of points should map to multiple distinct cells,
    // verifying the partitioning actually spreads data
    std::unordered_map<uint64_t, int> cell_counts;
    int level = 12;
    // 100 points spread across ~10km x ~10km around Beijing
    for (int i = 0; i < 10; i++) {
        for (int j = 0; j < 10; j++) {
            double lat = 39.85 + i * 0.01;
            double lng = 116.25 + j * 0.01;
            uint64_t cell = s2_cell_id_from_latlng(lat, lng, level);
            cell_counts[cell]++;
        }
    }
    // ~10km span at level 12 (~1.8km edge) should produce multiple cells
    ASSERT_GT(cell_counts.size(), 3) << "Points should spread across multiple cells";
    ASSERT_LT(cell_counts.size(), 100) << "Points should cluster, not be 1:1";
}

} // namespace starrocks
