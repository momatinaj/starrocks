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

#include "storage/index/vector/spatial_vector_index_writer.h"

#include <gtest/gtest.h>

#include <cstdlib>

#include "column/array_column.h"
#include "column/fixed_length_column.h"
#include "column/nullable_column.h"
#include "fs/fs_util.h"
#include "geo/s2_cell_utils.h"
#include "storage/index/index_descriptor.h"
#include "storage/index/vector/spatial_partition_meta.h"
#include "storage/tablet_index.h"
#include "util/json_util.h"

namespace starrocks {

class SpatialVectorIndexWriterTest : public ::testing::Test {
protected:
    void SetUp() override {
        _test_dir = fmt::format("/tmp/spatial_writer_test_{}", ::getpid());
        ::system(("rm -rf " + _test_dir).c_str());
        ::system(("mkdir -p " + _test_dir).c_str());
    }

    void TearDown() override { ::system(("rm -rf " + _test_dir).c_str()); }

    std::shared_ptr<TabletIndex> make_tablet_index(int s2_level = 12, int min_rows = 50) {
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
                                     {"min_partition_rows", std::to_string(min_rows)}};
        pb.set_index_properties(to_json(props));

        auto idx = std::make_shared<TabletIndex>();
        CHECK(idx->init_from_pb(pb).ok());
        return idx;
    }

    // Build an ArrayColumn<NullableColumn<FloatColumn>> with `num_rows` vectors of dimension `dim`.
    ColumnPtr make_test_vectors(int num_rows, int dim = 4) {
        auto elements = FloatColumn::create();
        auto null_col = NullColumn::create();
        auto offsets = UInt32Column::create();
        offsets->append(0);
        for (int i = 0; i < num_rows; i++) {
            for (int d = 0; d < dim; d++) {
                elements->append(static_cast<float>(i * dim + d));
                null_col->append(0);
            }
            offsets->append(static_cast<uint32_t>((i + 1) * dim));
        }
        auto nullable = NullableColumn::create(std::move(elements), std::move(null_col));
        return ArrayColumn::create(std::move(nullable), std::move(offsets));
    }

    StatusOr<std::string> read_file_bytes(const std::string& path) {
        ASSIGN_OR_RETURN(auto file, fs::new_random_access_file(path));
        ASSIGN_OR_RETURN(auto size, file->get_size());
        std::string buf(size, '\0');
        RETURN_IF_ERROR(file->read_fully(buf.data(), size));
        return buf;
    }

    StatusOr<std::vector<uint32_t>> read_row_id_map(const std::string& path) {
        ASSIGN_OR_RETURN(auto data, read_file_bytes(path));
        size_t count = data.size() / sizeof(uint32_t);
        std::vector<uint32_t> ids(count);
        std::memcpy(ids.data(), data.data(), data.size());
        return ids;
    }

    std::string _test_dir;
    static constexpr const char* kRowsetId = "rowset0";
    static constexpr int kSegmentId = 0;
};

TEST_F(SpatialVectorIndexWriterTest, test_empty_input) {
    auto idx = make_tablet_index();
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());
    EXPECT_EQ(writer.total_rows(), 0);
    EXPECT_EQ(writer.num_partitions(), 0);

    uint64_t index_size = 0;
    ASSERT_TRUE(writer.finish(&index_size).ok());
    EXPECT_EQ(index_size, 0);
}

TEST_F(SpatialVectorIndexWriterTest, test_single_partition) {
    auto idx = make_tablet_index(12, /*min_rows=*/2);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    int num_rows = 10;
    auto vectors = make_test_vectors(num_rows);
    uint64_t cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
    std::vector<uint64_t> cell_ids(num_rows, cell);

    ASSERT_TRUE(writer.append(*vectors, cell_ids).ok());
    EXPECT_EQ(writer.total_rows(), num_rows);
    EXPECT_EQ(writer.num_partitions(), 1);

    uint64_t index_size = 0;
    ASSERT_TRUE(writer.finish(&index_size).ok());

    // Manifest should exist
    auto manifest_path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    auto manifest_data = read_file_bytes(manifest_path);
    ASSERT_TRUE(manifest_data.ok()) << manifest_data.status().message();
    auto manifest = SpatialPartitionManifest::deserialize(*manifest_data);
    ASSERT_TRUE(manifest.ok());
    EXPECT_EQ(manifest->partition_count(), 1);
    EXPECT_EQ(manifest->partitions()[0].cell_id, cell);
    EXPECT_EQ(manifest->partitions()[0].row_count, num_rows);
    EXPECT_TRUE(manifest->partitions()[0].has_hnsw);

    // Row ID map should exist
    std::string token = s2_cell_id_to_token(cell);
    auto rowids_path = IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token);
    auto rowids = read_row_id_map(rowids_path);
    ASSERT_TRUE(rowids.ok());
    ASSERT_EQ(rowids->size(), num_rows);
    for (int i = 0; i < num_rows; i++) {
        EXPECT_EQ((*rowids)[i], static_cast<uint32_t>(i));
    }
}

TEST_F(SpatialVectorIndexWriterTest, test_two_partitions) {
    auto idx = make_tablet_index(12, /*min_rows=*/2);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(6);
    uint64_t sf_cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
    uint64_t ny_cell = s2_cell_id_from_latlng(40.7128, -74.0060, 12);
    ASSERT_NE(sf_cell, ny_cell);

    // rows 0,1,2 → SF; rows 3,4,5 → NY
    std::vector<uint64_t> cell_ids = {sf_cell, sf_cell, sf_cell, ny_cell, ny_cell, ny_cell};
    ASSERT_TRUE(writer.append(*vectors, cell_ids).ok());
    EXPECT_EQ(writer.num_partitions(), 2);

    uint64_t index_size = 0;
    ASSERT_TRUE(writer.finish(&index_size).ok());

    auto manifest_path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    auto manifest_data = read_file_bytes(manifest_path);
    ASSERT_TRUE(manifest_data.ok());
    auto manifest = SpatialPartitionManifest::deserialize(*manifest_data);
    ASSERT_TRUE(manifest.ok());
    EXPECT_EQ(manifest->partition_count(), 2);

    // Both partitions have 3 rows each, both above min_rows=2
    for (const auto& p : manifest->partitions()) {
        EXPECT_EQ(p.row_count, 3);
        EXPECT_TRUE(p.has_hnsw);
    }
}

TEST_F(SpatialVectorIndexWriterTest, test_tiny_partition_skips_hnsw) {
    auto idx = make_tablet_index(12, /*min_rows=*/100);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(10);
    uint64_t cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
    std::vector<uint64_t> cell_ids(10, cell);

    ASSERT_TRUE(writer.append(*vectors, cell_ids).ok());

    uint64_t index_size = 0;
    ASSERT_TRUE(writer.finish(&index_size).ok());

    auto manifest_path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    auto manifest = SpatialPartitionManifest::deserialize(read_file_bytes(manifest_path).value());
    ASSERT_TRUE(manifest.ok());
    EXPECT_EQ(manifest->partition_count(), 1);
    EXPECT_FALSE(manifest->partitions()[0].has_hnsw);

    // .vi file should NOT exist for tiny partition
    std::string token = s2_cell_id_to_token(cell);
    auto vi_path =
            IndexDescriptor::partitioned_vector_index_file_path(_test_dir, kRowsetId, kSegmentId, 42, token);
    EXPECT_FALSE(fs::path_exist(vi_path));

    // Row ID map SHOULD still exist
    auto rowids_path = IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token);
    EXPECT_TRUE(fs::path_exist(rowids_path));
}

TEST_F(SpatialVectorIndexWriterTest, test_mixed_hnsw_and_tiny) {
    auto idx = make_tablet_index(12, /*min_rows=*/5);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(8);
    uint64_t big_cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
    uint64_t tiny_cell = s2_cell_id_from_latlng(40.7128, -74.0060, 12);

    // 6 → big (above threshold), 2 → tiny (below threshold)
    std::vector<uint64_t> cell_ids = {big_cell, big_cell, big_cell, big_cell, big_cell, big_cell, tiny_cell, tiny_cell};
    ASSERT_TRUE(writer.append(*vectors, cell_ids).ok());

    uint64_t index_size = 0;
    ASSERT_TRUE(writer.finish(&index_size).ok());

    auto manifest_path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    auto manifest = SpatialPartitionManifest::deserialize(read_file_bytes(manifest_path).value());
    ASSERT_TRUE(manifest.ok());
    EXPECT_EQ(manifest->partition_count(), 2);

    bool found_big = false, found_tiny = false;
    for (const auto& p : manifest->partitions()) {
        if (p.cell_id == big_cell) {
            EXPECT_EQ(p.row_count, 6);
            EXPECT_TRUE(p.has_hnsw);
            found_big = true;
        } else if (p.cell_id == tiny_cell) {
            EXPECT_EQ(p.row_count, 2);
            EXPECT_FALSE(p.has_hnsw);
            found_tiny = true;
        }
    }
    EXPECT_TRUE(found_big);
    EXPECT_TRUE(found_tiny);
}

TEST_F(SpatialVectorIndexWriterTest, test_row_id_mapping_correctness) {
    auto idx = make_tablet_index(12, /*min_rows=*/2);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(6);
    uint64_t cell_a = s2_cell_id_from_latlng(37.7749, -122.4194, 12);
    uint64_t cell_b = s2_cell_id_from_latlng(40.7128, -74.0060, 12);

    // Interleave: A, B, A, B, A, B → each gets rows {0,2,4} and {1,3,5}
    std::vector<uint64_t> cell_ids = {cell_a, cell_b, cell_a, cell_b, cell_a, cell_b};
    ASSERT_TRUE(writer.append(*vectors, cell_ids).ok());
    ASSERT_TRUE(writer.finish(nullptr).ok());

    std::string token_a = s2_cell_id_to_token(cell_a);
    auto rowids_a = read_row_id_map(
            IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token_a));
    ASSERT_TRUE(rowids_a.ok());
    EXPECT_EQ(*rowids_a, (std::vector<uint32_t>{0, 2, 4}));

    std::string token_b = s2_cell_id_to_token(cell_b);
    auto rowids_b = read_row_id_map(
            IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token_b));
    ASSERT_TRUE(rowids_b.ok());
    EXPECT_EQ(*rowids_b, (std::vector<uint32_t>{1, 3, 5}));
}

TEST_F(SpatialVectorIndexWriterTest, test_multiple_appends) {
    auto idx = make_tablet_index(12, /*min_rows=*/2);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    uint64_t cell = s2_cell_id_from_latlng(37.7749, -122.4194, 12);

    auto v1 = make_test_vectors(3);
    ASSERT_TRUE(writer.append(*v1, {cell, cell, cell}).ok());
    EXPECT_EQ(writer.total_rows(), 3);

    auto v2 = make_test_vectors(4);
    ASSERT_TRUE(writer.append(*v2, {cell, cell, cell, cell}).ok());
    EXPECT_EQ(writer.total_rows(), 7);
    EXPECT_EQ(writer.num_partitions(), 1);

    ASSERT_TRUE(writer.finish(nullptr).ok());

    auto manifest_path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    auto manifest = SpatialPartitionManifest::deserialize(read_file_bytes(manifest_path).value());
    ASSERT_TRUE(manifest.ok());
    EXPECT_EQ(manifest->partitions()[0].row_count, 7);

    // Row IDs should be contiguous 0..6
    std::string token = s2_cell_id_to_token(cell);
    auto rowids = read_row_id_map(
            IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token));
    ASSERT_TRUE(rowids.ok());
    ASSERT_EQ(rowids->size(), 7);
    for (uint32_t i = 0; i < 7; i++) {
        EXPECT_EQ((*rowids)[i], i);
    }
}

TEST_F(SpatialVectorIndexWriterTest, test_cell_ids_size_mismatch) {
    auto idx = make_tablet_index();
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(5);
    std::vector<uint64_t> wrong_size = {1, 2, 3};
    auto st = writer.append(*vectors, wrong_size);
    EXPECT_FALSE(st.ok());
}

TEST_F(SpatialVectorIndexWriterTest, test_file_paths_correct) {
    auto idx = make_tablet_index(12, /*min_rows=*/2);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(5);
    uint64_t cell = s2_cell_id_from_latlng(51.5074, -0.1278, 12);
    std::vector<uint64_t> cell_ids(5, cell);
    ASSERT_TRUE(writer.append(*vectors, cell_ids).ok());
    ASSERT_TRUE(writer.finish(nullptr).ok());

    std::string token = s2_cell_id_to_token(cell);

    // .vi file
    auto vi = IndexDescriptor::partitioned_vector_index_file_path(_test_dir, kRowsetId, kSegmentId, 42, token);
    EXPECT_TRUE(fs::path_exist(vi)) << vi;

    // .vi_rowids file
    auto rowids = IndexDescriptor::partition_rowid_map_file_path(_test_dir, kRowsetId, kSegmentId, 42, token);
    EXPECT_TRUE(fs::path_exist(rowids)) << rowids;

    // .vi_manifest file
    auto mf = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    EXPECT_TRUE(fs::path_exist(mf)) << mf;
}

TEST_F(SpatialVectorIndexWriterTest, test_manifest_s2_level_propagated) {
    int level = 16;
    auto idx = make_tablet_index(level, /*min_rows=*/2);
    SpatialVectorIndexWriter writer(idx, _test_dir, kRowsetId, kSegmentId, true);
    ASSERT_TRUE(writer.init().ok());

    auto vectors = make_test_vectors(3);
    uint64_t cell = s2_cell_id_from_latlng(48.8566, 2.3522, level);
    ASSERT_TRUE(writer.append(*vectors, {cell, cell, cell}).ok());
    ASSERT_TRUE(writer.finish(nullptr).ok());

    auto manifest_path = IndexDescriptor::partition_manifest_file_path(_test_dir, kRowsetId, kSegmentId, 42);
    auto manifest = SpatialPartitionManifest::deserialize(read_file_bytes(manifest_path).value());
    ASSERT_TRUE(manifest.ok());
    EXPECT_EQ(manifest->s2_level(), level);
    EXPECT_EQ(manifest->spatial_lat_column_uid(), 5);
    EXPECT_EQ(manifest->spatial_lng_column_uid(), 6);
}

} // namespace starrocks
