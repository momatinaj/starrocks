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

#include "storage/index/vector/spatial_partition_meta.h"

#include <gtest/gtest.h>

#include "geo/s2_cell_utils.h"
#include "storage/index/index_descriptor.h"

namespace starrocks {

class SpatialPartitionMetaTest : public ::testing::Test {
protected:
    std::map<std::string, std::string> make_valid_properties(int s2_level = 12, int lat_uid = 5, int lng_uid = 6,
                                                             int min_rows = 100) {
        return {{SpatialIndexPropertyKeys::kIsSpatialPartitioned, "true"},
                {SpatialIndexPropertyKeys::kS2Level, std::to_string(s2_level)},
                {SpatialIndexPropertyKeys::kSpatialLatColumnUid, std::to_string(lat_uid)},
                {SpatialIndexPropertyKeys::kSpatialLngColumnUid, std::to_string(lng_uid)},
                {SpatialIndexPropertyKeys::kMinPartitionRows, std::to_string(min_rows)}};
    }

    SpatialPartitionManifest make_sample_manifest() {
        SpatialPartitionManifest m(12, 5, 6, 100);
        uint64_t cell_a = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
        uint64_t cell_b = s2_cell_id_from_latlng(40.7128, -74.0060, 12);
        m.add_partition({cell_a, 500, 0, true});
        m.add_partition({cell_b, 50, 500, false});
        return m;
    }
};

// ========================================================================
// init_from_properties
// ========================================================================

TEST_F(SpatialPartitionMetaTest, init_from_valid_properties) {
    auto props = make_valid_properties(14, 10, 11, 200);
    SpatialPartitionManifest m;
    ASSERT_TRUE(m.init_from_properties(props).ok());
    EXPECT_EQ(m.s2_level(), 14);
    EXPECT_EQ(m.spatial_lat_column_uid(), 10);
    EXPECT_EQ(m.spatial_lng_column_uid(), 11);
    EXPECT_EQ(m.min_partition_rows(), 200);
    EXPECT_EQ(m.partition_count(), 0);
}

TEST_F(SpatialPartitionMetaTest, init_missing_is_spatial_partitioned) {
    auto props = make_valid_properties();
    props.erase(SpatialIndexPropertyKeys::kIsSpatialPartitioned);
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_is_spatial_partitioned_false) {
    auto props = make_valid_properties();
    props[SpatialIndexPropertyKeys::kIsSpatialPartitioned] = "false";
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_missing_s2_level) {
    auto props = make_valid_properties();
    props.erase(SpatialIndexPropertyKeys::kS2Level);
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_invalid_s2_level_negative) {
    auto props = make_valid_properties();
    props[SpatialIndexPropertyKeys::kS2Level] = "-1";
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_invalid_s2_level_too_high) {
    auto props = make_valid_properties();
    props[SpatialIndexPropertyKeys::kS2Level] = "31";
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_missing_lat_column) {
    auto props = make_valid_properties();
    props.erase(SpatialIndexPropertyKeys::kSpatialLatColumnUid);
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_missing_lng_column) {
    auto props = make_valid_properties();
    props.erase(SpatialIndexPropertyKeys::kSpatialLngColumnUid);
    SpatialPartitionManifest m;
    EXPECT_FALSE(m.init_from_properties(props).ok());
}

TEST_F(SpatialPartitionMetaTest, init_default_min_partition_rows) {
    auto props = make_valid_properties();
    props.erase(SpatialIndexPropertyKeys::kMinPartitionRows);
    SpatialPartitionManifest m;
    ASSERT_TRUE(m.init_from_properties(props).ok());
    EXPECT_EQ(m.min_partition_rows(), SpatialPartitionManifest::kDefaultMinPartitionRows);
}

TEST_F(SpatialPartitionMetaTest, init_boundary_s2_levels) {
    for (int level : {0, 15, 30}) {
        auto props = make_valid_properties(level);
        SpatialPartitionManifest m;
        ASSERT_TRUE(m.init_from_properties(props).ok()) << "level=" << level;
        EXPECT_EQ(m.s2_level(), level);
    }
}

// ========================================================================
// to_properties roundtrip
// ========================================================================

TEST_F(SpatialPartitionMetaTest, to_properties_roundtrip) {
    auto original_props = make_valid_properties(16, 7, 8, 250);
    SpatialPartitionManifest m;
    ASSERT_TRUE(m.init_from_properties(original_props).ok());

    std::map<std::string, std::string> exported;
    m.to_properties(&exported);

    SpatialPartitionManifest m2;
    ASSERT_TRUE(m2.init_from_properties(exported).ok());
    EXPECT_EQ(m2.s2_level(), 16);
    EXPECT_EQ(m2.spatial_lat_column_uid(), 7);
    EXPECT_EQ(m2.spatial_lng_column_uid(), 8);
    EXPECT_EQ(m2.min_partition_rows(), 250);
}

// ========================================================================
// Partition management
// ========================================================================

TEST_F(SpatialPartitionMetaTest, add_and_count_partitions) {
    SpatialPartitionManifest m(12, 5, 6, 100);
    EXPECT_EQ(m.partition_count(), 0);

    m.add_partition({100, 50, 0, false});
    EXPECT_EQ(m.partition_count(), 1);

    m.add_partition({200, 200, 50, true});
    EXPECT_EQ(m.partition_count(), 2);

    const auto& partitions = m.partitions();
    EXPECT_EQ(partitions[0].cell_id, 100);
    EXPECT_EQ(partitions[0].row_count, 50);
    EXPECT_EQ(partitions[0].first_row_id, 0);
    EXPECT_FALSE(partitions[0].has_hnsw);
    EXPECT_EQ(partitions[1].cell_id, 200);
    EXPECT_TRUE(partitions[1].has_hnsw);
}

// ========================================================================
// find_matching_partitions
// ========================================================================

TEST_F(SpatialPartitionMetaTest, find_matching_partitions_basic) {
    auto m = make_sample_manifest();
    auto cell_a = m.partitions()[0].cell_id;
    auto cell_b = m.partitions()[1].cell_id;

    auto matches = m.find_matching_partitions({cell_a});
    ASSERT_EQ(matches.size(), 1);
    EXPECT_EQ(matches[0]->cell_id, cell_a);

    matches = m.find_matching_partitions({cell_a, cell_b});
    EXPECT_EQ(matches.size(), 2);
}

TEST_F(SpatialPartitionMetaTest, find_matching_partitions_no_match) {
    auto m = make_sample_manifest();
    auto matches = m.find_matching_partitions({999999});
    EXPECT_TRUE(matches.empty());
}

TEST_F(SpatialPartitionMetaTest, find_matching_partitions_empty_query) {
    auto m = make_sample_manifest();
    auto matches = m.find_matching_partitions({});
    EXPECT_TRUE(matches.empty());
}

TEST_F(SpatialPartitionMetaTest, find_matching_partitions_empty_manifest) {
    SpatialPartitionManifest m(12, 5, 6, 100);
    auto matches = m.find_matching_partitions({100, 200});
    EXPECT_TRUE(matches.empty());
}

// ========================================================================
// Binary serialization roundtrip
// ========================================================================

TEST_F(SpatialPartitionMetaTest, serialize_deserialize_roundtrip) {
    auto original = make_sample_manifest();
    std::string buf;
    ASSERT_TRUE(original.serialize(&buf).ok());
    EXPECT_EQ(buf.size(), original.serialized_size());

    auto result = SpatialPartitionManifest::deserialize(buf);
    ASSERT_TRUE(result.ok()) << result.status().message();
    EXPECT_EQ(*result, original);
}

TEST_F(SpatialPartitionMetaTest, serialize_empty_manifest) {
    SpatialPartitionManifest m(14, 3, 4, 50);
    std::string buf;
    ASSERT_TRUE(m.serialize(&buf).ok());
    EXPECT_EQ(buf.size(), SpatialPartitionManifest::kHeaderSize);

    auto result = SpatialPartitionManifest::deserialize(buf);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(result->partition_count(), 0);
    EXPECT_EQ(result->s2_level(), 14);
}

TEST_F(SpatialPartitionMetaTest, serialize_many_partitions) {
    SpatialPartitionManifest m(10, 1, 2, 50);
    for (uint32_t i = 0; i < 1000; i++) {
        double lat = -90.0 + (180.0 * i / 1000.0);
        double lng = -180.0 + (360.0 * i / 1000.0);
        uint64_t cell_id = s2_cell_id_from_latlng(lat, lng, 10);
        m.add_partition({cell_id, i * 10, i * 100, i % 2 == 0});
    }

    std::string buf;
    ASSERT_TRUE(m.serialize(&buf).ok());

    auto result = SpatialPartitionManifest::deserialize(buf);
    ASSERT_TRUE(result.ok());
    EXPECT_EQ(*result, m);
}

TEST_F(SpatialPartitionMetaTest, deserialize_truncated_header) {
    std::string truncated(10, '\0');
    auto result = SpatialPartitionManifest::deserialize(truncated);
    EXPECT_FALSE(result.ok());
}

TEST_F(SpatialPartitionMetaTest, deserialize_bad_magic) {
    auto m = make_sample_manifest();
    std::string buf;
    ASSERT_TRUE(m.serialize(&buf).ok());
    buf[0] = 0xFF;
    auto result = SpatialPartitionManifest::deserialize(buf);
    EXPECT_FALSE(result.ok());
}

TEST_F(SpatialPartitionMetaTest, deserialize_bad_version) {
    auto m = make_sample_manifest();
    std::string buf;
    ASSERT_TRUE(m.serialize(&buf).ok());
    // version is at offset 4-7, set to 99
    uint32_t bad_version = 99;
    std::memcpy(&buf[4], &bad_version, sizeof(uint32_t));
    auto result = SpatialPartitionManifest::deserialize(buf);
    EXPECT_FALSE(result.ok());
}

TEST_F(SpatialPartitionMetaTest, deserialize_truncated_partitions) {
    auto m = make_sample_manifest();
    std::string buf;
    ASSERT_TRUE(m.serialize(&buf).ok());
    buf.resize(SpatialPartitionManifest::kHeaderSize + 5);
    auto result = SpatialPartitionManifest::deserialize(buf);
    EXPECT_FALSE(result.ok());
}

// ========================================================================
// SpatialPartitionInfo equality
// ========================================================================

TEST_F(SpatialPartitionMetaTest, partition_info_equality) {
    SpatialPartitionInfo a{100, 50, 0, true};
    SpatialPartitionInfo b{100, 50, 0, true};
    SpatialPartitionInfo c{100, 50, 0, false};
    SpatialPartitionInfo d{200, 50, 0, true};
    EXPECT_EQ(a, b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
}

// ========================================================================
// IndexDescriptor path helpers
// ========================================================================

TEST_F(SpatialPartitionMetaTest, partitioned_vi_file_path) {
    auto path = IndexDescriptor::partitioned_vector_index_file_path("/data/rowset", "abc123", 0, 42, "89c25c");
    EXPECT_EQ(path, "/data/rowset/abc123_0_42.89c25c.vi");
}

TEST_F(SpatialPartitionMetaTest, partitioned_vi_file_path_distinct_tokens) {
    auto path_a = IndexDescriptor::partitioned_vector_index_file_path("/d", "r", 1, 7, "aaa");
    auto path_b = IndexDescriptor::partitioned_vector_index_file_path("/d", "r", 1, 7, "bbb");
    EXPECT_NE(path_a, path_b);
    EXPECT_EQ(path_a, "/d/r_1_7.aaa.vi");
    EXPECT_EQ(path_b, "/d/r_1_7.bbb.vi");
}

TEST_F(SpatialPartitionMetaTest, manifest_file_path) {
    auto path = IndexDescriptor::partition_manifest_file_path("/data/rowset", "abc123", 0, 42);
    EXPECT_EQ(path, "/data/rowset/abc123_0_42.vi_manifest");
}

TEST_F(SpatialPartitionMetaTest, standard_vi_path_unchanged) {
    auto path = IndexDescriptor::vector_index_file_path("/data/rowset", "abc123", 0, 42);
    EXPECT_EQ(path, "/data/rowset/abc123_0_42.vi");
}

// ========================================================================
// Integration: S2 cell IDs in partitions match routing
// ========================================================================

TEST_F(SpatialPartitionMetaTest, s2_routing_integration) {
    const int level = 12;
    const double sf_lat = 37.7749, sf_lng = -122.4194;
    const double ny_lat = 40.7128, ny_lng = -74.0060;

    SpatialPartitionManifest m(level, 5, 6, 100);
    uint64_t sf_cell = s2_cell_id_from_latlng(sf_lat, sf_lng, level);
    uint64_t ny_cell = s2_cell_id_from_latlng(ny_lat, ny_lng, level);
    m.add_partition({sf_cell, 1000, 0, true});
    m.add_partition({ny_cell, 2000, 1000, true});

    // Query near SF should match SF partition only
    auto sf_covering = s2_covering_cell_ids_for_cap(sf_lat, sf_lng, 100.0, level);
    auto matches = m.find_matching_partitions(sf_covering);
    ASSERT_GE(matches.size(), 1);
    bool found_sf = false;
    for (auto* p : matches) {
        if (p->cell_id == sf_cell) found_sf = true;
        EXPECT_NE(p->cell_id, ny_cell);
    }
    EXPECT_TRUE(found_sf);
}

TEST_F(SpatialPartitionMetaTest, end_to_end_path_with_s2_tokens) {
    const int level = 12;
    uint64_t cell_id = s2_cell_id_from_latlng(37.7749, -122.4194, level);
    std::string token = s2_cell_id_to_token(cell_id);

    auto path = IndexDescriptor::partitioned_vector_index_file_path("/data", "rowset0", 0, 1, token);
    EXPECT_TRUE(path.find(token) != std::string::npos);
    EXPECT_TRUE(path.find(".vi") != std::string::npos);

    uint64_t recovered_id = s2_cell_id_from_token(token);
    EXPECT_EQ(recovered_id, cell_id);
}

// ========================================================================
// has_hnsw flag serialization
// ========================================================================

TEST_F(SpatialPartitionMetaTest, has_hnsw_flag_preserved) {
    SpatialPartitionManifest m(12, 1, 2, 100);
    m.add_partition({111, 500, 0, true});
    m.add_partition({222, 50, 500, false});
    m.add_partition({333, 200, 550, true});
    m.add_partition({444, 10, 750, false});

    std::string buf;
    ASSERT_TRUE(m.serialize(&buf).ok());
    auto result = SpatialPartitionManifest::deserialize(buf);
    ASSERT_TRUE(result.ok());

    const auto& parts = result->partitions();
    ASSERT_EQ(parts.size(), 4);
    EXPECT_TRUE(parts[0].has_hnsw);
    EXPECT_FALSE(parts[1].has_hnsw);
    EXPECT_TRUE(parts[2].has_hnsw);
    EXPECT_FALSE(parts[3].has_hnsw);
}

// ========================================================================
// Property key constants are non-empty
// ========================================================================

TEST_F(SpatialPartitionMetaTest, property_keys_are_well_defined) {
    EXPECT_NE(std::string(SpatialIndexPropertyKeys::kIsSpatialPartitioned), "");
    EXPECT_NE(std::string(SpatialIndexPropertyKeys::kS2Level), "");
    EXPECT_NE(std::string(SpatialIndexPropertyKeys::kSpatialLatColumnUid), "");
    EXPECT_NE(std::string(SpatialIndexPropertyKeys::kSpatialLngColumnUid), "");
    EXPECT_NE(std::string(SpatialIndexPropertyKeys::kMinPartitionRows), "");
}

// ========================================================================
// serialized_size consistency
// ========================================================================

TEST_F(SpatialPartitionMetaTest, serialized_size_matches_actual) {
    for (uint32_t n : {0, 1, 5, 100}) {
        SpatialPartitionManifest m(12, 1, 2, 100);
        for (uint32_t i = 0; i < n; i++) {
            m.add_partition({i + 1, i * 10, i * 100, i % 2 == 0});
        }
        std::string buf;
        ASSERT_TRUE(m.serialize(&buf).ok());
        EXPECT_EQ(buf.size(), m.serialized_size()) << "n=" << n;
    }
}

} // namespace starrocks
