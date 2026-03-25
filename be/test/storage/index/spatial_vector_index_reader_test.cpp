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

#include "storage/index/vector/spatial_vector_index_reader.h"

#include <gtest/gtest.h>

#include <cstdlib>

#include "fs/fs_util.h"
#include "geo/s2_cell_utils.h"
#include "storage/index/index_descriptor.h"
#include "storage/index/vector/spatial_partition_meta.h"
#include "storage/tablet_index.h"
#include "util/json_util.h"

namespace starrocks {

// ========================================================================
// merge_partition_results tests (no TenANN dependency)
// ========================================================================

class SpatialVectorIndexReaderMergeTest : public ::testing::Test {};

TEST_F(SpatialVectorIndexReaderMergeTest, single_partition_topk) {
    std::vector<uint32_t> rowmap = {10, 20, 30, 40, 50};

    SpatialVectorIndexReader::PartitionResult pr;
    pr.local_ids = {0, 1, 2, 3, 4};
    pr.distances = {0.5f, 0.1f, 0.9f, 0.3f, 0.7f};
    pr.row_id_map = &rowmap;

    std::vector<int64_t> ids;
    std::vector<float> dists;
    SpatialVectorIndexReader::merge_partition_results({pr}, 3, &ids, &dists);

    ASSERT_EQ(ids.size(), 3);
    // Top-3 by ascending distance: 0.1 (id=20), 0.3 (id=40), 0.5 (id=10)
    EXPECT_EQ(ids[0], 20);
    EXPECT_FLOAT_EQ(dists[0], 0.1f);
    EXPECT_EQ(ids[1], 40);
    EXPECT_FLOAT_EQ(dists[1], 0.3f);
    EXPECT_EQ(ids[2], 10);
    EXPECT_FLOAT_EQ(dists[2], 0.5f);
}

TEST_F(SpatialVectorIndexReaderMergeTest, two_partitions_merge) {
    std::vector<uint32_t> map_a = {0, 2, 4};
    std::vector<uint32_t> map_b = {1, 3, 5};

    SpatialVectorIndexReader::PartitionResult pr_a;
    pr_a.local_ids = {0, 1, 2};
    pr_a.distances = {0.2f, 0.8f, 0.4f};
    pr_a.row_id_map = &map_a;

    SpatialVectorIndexReader::PartitionResult pr_b;
    pr_b.local_ids = {0, 1, 2};
    pr_b.distances = {0.1f, 0.5f, 0.3f};
    pr_b.row_id_map = &map_b;

    std::vector<int64_t> ids;
    std::vector<float> dists;
    SpatialVectorIndexReader::merge_partition_results({pr_a, pr_b}, 4, &ids, &dists);

    ASSERT_EQ(ids.size(), 4);
    // Global top-4: 0.1(seg=1), 0.2(seg=0), 0.3(seg=5), 0.4(seg=4)
    EXPECT_EQ(ids[0], 1);
    EXPECT_FLOAT_EQ(dists[0], 0.1f);
    EXPECT_EQ(ids[1], 0);
    EXPECT_FLOAT_EQ(dists[1], 0.2f);
    EXPECT_EQ(ids[2], 5);
    EXPECT_FLOAT_EQ(dists[2], 0.3f);
    EXPECT_EQ(ids[3], 4);
    EXPECT_FLOAT_EQ(dists[3], 0.4f);
}

TEST_F(SpatialVectorIndexReaderMergeTest, k_larger_than_total) {
    std::vector<uint32_t> rowmap = {100, 200};

    SpatialVectorIndexReader::PartitionResult pr;
    pr.local_ids = {0, 1};
    pr.distances = {1.0f, 2.0f};
    pr.row_id_map = &rowmap;

    std::vector<int64_t> ids;
    std::vector<float> dists;
    SpatialVectorIndexReader::merge_partition_results({pr}, 10, &ids, &dists);

    ASSERT_EQ(ids.size(), 2);
    EXPECT_EQ(ids[0], 100);
    EXPECT_EQ(ids[1], 200);
}

TEST_F(SpatialVectorIndexReaderMergeTest, skip_negative_ids) {
    std::vector<uint32_t> rowmap = {10, 20, 30};

    SpatialVectorIndexReader::PartitionResult pr;
    pr.local_ids = {0, -1, 2};
    pr.distances = {0.5f, 999.0f, 0.1f};
    pr.row_id_map = &rowmap;

    std::vector<int64_t> ids;
    std::vector<float> dists;
    SpatialVectorIndexReader::merge_partition_results({pr}, 5, &ids, &dists);

    ASSERT_EQ(ids.size(), 2);
    EXPECT_EQ(ids[0], 30);
    EXPECT_FLOAT_EQ(dists[0], 0.1f);
    EXPECT_EQ(ids[1], 10);
    EXPECT_FLOAT_EQ(dists[1], 0.5f);
}

TEST_F(SpatialVectorIndexReaderMergeTest, empty_results) {
    std::vector<int64_t> ids;
    std::vector<float> dists;
    SpatialVectorIndexReader::merge_partition_results({}, 10, &ids, &dists);

    EXPECT_TRUE(ids.empty());
    EXPECT_TRUE(dists.empty());
}

TEST_F(SpatialVectorIndexReaderMergeTest, many_partitions_merge) {
    std::vector<std::vector<uint32_t>> maps(5);
    std::vector<SpatialVectorIndexReader::PartitionResult> results(5);

    for (int p = 0; p < 5; p++) {
        maps[p] = {static_cast<uint32_t>(p * 100), static_cast<uint32_t>(p * 100 + 1)};
        results[p].local_ids = {0, 1};
        results[p].distances = {static_cast<float>(p) * 0.1f + 0.05f, static_cast<float>(p) * 0.1f + 0.15f};
        results[p].row_id_map = &maps[p];
    }

    std::vector<int64_t> ids;
    std::vector<float> dists;
    SpatialVectorIndexReader::merge_partition_results(results, 3, &ids, &dists);

    ASSERT_EQ(ids.size(), 3);
    // Smallest distances: 0.05 (p0), 0.15 (p0/p1), 0.15 (p1)
    EXPECT_FLOAT_EQ(dists[0], 0.05f);
    EXPECT_EQ(ids[0], 0);
}

// ========================================================================
// translate_row_id tests
// ========================================================================

class SpatialVectorIndexReaderTranslateTest : public ::testing::Test {};

TEST_F(SpatialVectorIndexReaderTranslateTest, basic_translation) {
    SpatialVectorIndexReader reader;
    // Manually set up partitions for testing (via init with test files)
    // We test translate_row_id indirectly through merge tests above,
    // and directly through the init-based tests below.
}

// ========================================================================
// Init + file loading tests (creates temp files)
// ========================================================================

class SpatialVectorIndexReaderInitTest : public ::testing::Test {
protected:
    void SetUp() override {
        _test_dir = fmt::format("/tmp/spatial_reader_test_{}", ::getpid());
        ::system(("rm -rf " + _test_dir).c_str());
        ::system(("mkdir -p " + _test_dir).c_str());
    }

    void TearDown() override { ::system(("rm -rf " + _test_dir).c_str()); }

    std::shared_ptr<TabletIndex> make_tablet_index(int s2_level = 12) {
        TabletIndexPB pb;
        pb.set_index_id(42);
        pb.set_index_name("test_spatial_vec");
        pb.set_index_type(IndexType::VECTOR);

        std::map<std::string, std::map<std::string, std::string>> props;
        props["common_properties"] = {{"index_type", "hnsw"}};
        props["index_properties"] = {{"is_spatial_partitioned", "true"},
                                     {"s2_level", std::to_string(s2_level)},
                                     {"spatial_lat_column_uid", "5"},
                                     {"spatial_lng_column_uid", "6"},
                                     {"min_partition_rows", "50"}};
        pb.set_index_properties(to_json(props));

        auto idx = std::make_shared<TabletIndex>();
        CHECK(idx->init_from_pb(pb).ok());
        return idx;
    }

    Status write_manifest(const SpatialPartitionManifest& manifest) {
        std::string buf;
        RETURN_IF_ERROR(manifest.serialize(&buf));
        auto path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
        ASSIGN_OR_RETURN(auto file, fs::new_writable_file(path));
        RETURN_IF_ERROR(file->append(Slice(buf)));
        RETURN_IF_ERROR(file->flush(WritableFile::FLUSH_SYNC));
        return file->close();
    }

    Status write_row_id_map(uint64_t cell_id, const std::vector<uint32_t>& row_ids) {
        std::string token = s2_cell_id_to_token(cell_id);
        auto path = IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token);
        ASSIGN_OR_RETURN(auto file, fs::new_writable_file(path));
        Slice data(reinterpret_cast<const char*>(row_ids.data()), row_ids.size() * sizeof(uint32_t));
        RETURN_IF_ERROR(file->append(data));
        RETURN_IF_ERROR(file->flush(WritableFile::FLUSH_SYNC));
        return file->close();
    }

    std::string _test_dir;
    static constexpr const char* kRowsetId = "rowset0";
    static constexpr int kSegmentId = 0;
};

TEST_F(SpatialVectorIndexReaderInitTest, no_manifest_returns_invalid) {
    auto idx = make_tablet_index();
    SpatialVectorIndexReader reader;
    ASSERT_TRUE(reader.init(_test_dir, kRowsetId, kSegmentId, 42, idx, {}).ok());
    EXPECT_FALSE(reader.is_valid());
}

TEST_F(SpatialVectorIndexReaderInitTest, loads_manifest_and_rowmaps) {
    uint64_t cell_a = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
    uint64_t cell_b = s2_cell_id_from_latlng(40.7128, -74.0060, 12);

    SpatialPartitionManifest manifest(12, 5, 6, 50);
    manifest.add_partition({cell_a, 3, 0, false});
    manifest.add_partition({cell_b, 5, 3, false});

    ASSERT_TRUE(write_manifest(manifest).ok());
    ASSERT_TRUE(write_row_id_map(cell_a, {0, 1, 2}).ok());
    ASSERT_TRUE(write_row_id_map(cell_b, {3, 4, 5, 6, 7}).ok());

    auto idx = make_tablet_index();
    SpatialVectorIndexReader reader;
    ASSERT_TRUE(reader.init(_test_dir, kRowsetId, kSegmentId, 42, idx, {}).ok());
    EXPECT_TRUE(reader.is_valid());
    EXPECT_EQ(reader.manifest().partition_count(), 2);

    const auto& parts = reader.partitions();
    ASSERT_EQ(parts.size(), 2);
    EXPECT_EQ(parts[0].segment_row_ids, (std::vector<uint32_t>{0, 1, 2}));
    EXPECT_EQ(parts[1].segment_row_ids, (std::vector<uint32_t>{3, 4, 5, 6, 7}));
}

TEST_F(SpatialVectorIndexReaderInitTest, translate_row_id_works) {
    uint64_t cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);

    SpatialPartitionManifest manifest(12, 5, 6, 50);
    manifest.add_partition({cell, 4, 0, false});

    ASSERT_TRUE(write_manifest(manifest).ok());
    ASSERT_TRUE(write_row_id_map(cell, {10, 20, 30, 40}).ok());

    auto idx = make_tablet_index();
    SpatialVectorIndexReader reader;
    ASSERT_TRUE(reader.init(_test_dir, kRowsetId, kSegmentId, 42, idx, {}).ok());

    EXPECT_EQ(reader.translate_row_id(0, 0), 10);
    EXPECT_EQ(reader.translate_row_id(0, 1), 20);
    EXPECT_EQ(reader.translate_row_id(0, 2), 30);
    EXPECT_EQ(reader.translate_row_id(0, 3), 40);
    EXPECT_EQ(reader.translate_row_id(0, 4), -1);  // out of range
    EXPECT_EQ(reader.translate_row_id(0, -1), -1);  // negative
    EXPECT_EQ(reader.translate_row_id(1, 0), -1);   // invalid partition
}

TEST_F(SpatialVectorIndexReaderInitTest, corrupt_rowmap_detected) {
    uint64_t cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);

    SpatialPartitionManifest manifest(12, 5, 6, 50);
    manifest.add_partition({cell, 5, 0, false});

    ASSERT_TRUE(write_manifest(manifest).ok());
    ASSERT_TRUE(write_row_id_map(cell, {10, 20, 30}).ok()); // only 3, expected 5

    auto idx = make_tablet_index();
    SpatialVectorIndexReader reader;
    auto st = reader.init(_test_dir, kRowsetId, kSegmentId, 42, idx, {});
    EXPECT_FALSE(st.ok());
}

TEST_F(SpatialVectorIndexReaderInitTest, active_reader_count_without_tenann) {
    uint64_t cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);

    SpatialPartitionManifest manifest(12, 5, 6, 50);
    manifest.add_partition({cell, 3, 0, false}); // has_hnsw = false

    ASSERT_TRUE(write_manifest(manifest).ok());
    ASSERT_TRUE(write_row_id_map(cell, {0, 1, 2}).ok());

    auto idx = make_tablet_index();
    SpatialVectorIndexReader reader;
    ASSERT_TRUE(reader.init(_test_dir, kRowsetId, kSegmentId, 42, idx, {}).ok());

    EXPECT_EQ(reader.active_reader_count(), 0);
}

} // namespace starrocks
