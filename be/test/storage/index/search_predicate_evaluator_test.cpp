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

#include "storage/index/vector/search_predicate_evaluator.h"

#include <gtest/gtest.h>

#include <cmath>

namespace starrocks {

class SearchPredicateEvaluatorTest : public testing::Test {
protected:
    // San Francisco city center
    static constexpr double kSFLat = 37.7749;
    static constexpr double kSFLng = -122.4194;

    // A point ~1km north of SF center
    static constexpr double kNearLat = 37.7839;
    static constexpr double kNearLng = -122.4194;

    // A point ~50km away (San Jose)
    static constexpr double kFarLat = 37.3382;
    static constexpr double kFarLng = -121.8863;
};

// ---- Radius Evaluator Tests ----

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_inside) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 5000; // 5km

    std::vector<double> lats = {kNearLat};
    std::vector<double> lngs = {kNearLng};

    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_TRUE(eval.evaluate(0)); // ~1km < 5km
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_outside) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 5000;

    std::vector<double> lats = {kFarLat};
    std::vector<double> lngs = {kFarLng};

    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_FALSE(eval.evaluate(0)); // ~50km > 5km
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_multiple_points) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 5000;

    std::vector<double> lats = {kNearLat, kFarLat, kSFLat};
    std::vector<double> lngs = {kNearLng, kFarLng, kSFLng};

    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_TRUE(eval.evaluate(0));  // near: inside
    ASSERT_FALSE(eval.evaluate(1)); // far: outside
    ASSERT_TRUE(eval.evaluate(2));  // center: inside (dist=0)
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_out_of_range) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 5000;

    std::vector<double> lats = {kSFLat};
    std::vector<double> lngs = {kSFLng};

    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_FALSE(eval.evaluate(-1)); // invalid row_id
    ASSERT_FALSE(eval.evaluate(1));  // out of range
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_data_mismatch) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 5000;

    std::vector<double> lats = {kSFLat, kNearLat};
    std::vector<double> lngs = {kSFLng};

    SpatialRadiusEvaluator eval;
    ASSERT_FALSE(eval.init(spec, lats, lngs).ok()); // size mismatch
}

// ---- Polygon Evaluator Tests ----

TEST_F(SearchPredicateEvaluatorTest, test_polygon_evaluator_inside) {
    // A square around SF center: ~10km bounding box
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    spec.wkt = "POLYGON((-122.5 37.7, -122.3 37.7, -122.3 37.85, -122.5 37.85, -122.5 37.7))";

    std::vector<double> lats = {kSFLat}; // 37.7749
    std::vector<double> lngs = {kSFLng}; // -122.4194

    SpatialPolygonEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_TRUE(eval.evaluate(0)); // SF center is inside the box
}

TEST_F(SearchPredicateEvaluatorTest, test_polygon_evaluator_outside) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    spec.wkt = "POLYGON((-122.5 37.7, -122.3 37.7, -122.3 37.85, -122.5 37.85, -122.5 37.7))";

    std::vector<double> lats = {kFarLat}; // San Jose: 37.3382
    std::vector<double> lngs = {kFarLng}; // -121.8863

    SpatialPolygonEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_FALSE(eval.evaluate(0)); // San Jose is outside the SF box
}

TEST_F(SearchPredicateEvaluatorTest, test_polygon_evaluator_invalid_wkt) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    spec.wkt = "NOT_A_POLYGON";

    std::vector<double> lats = {kSFLat};
    std::vector<double> lngs = {kSFLng};

    SpatialPolygonEvaluator eval;
    ASSERT_FALSE(eval.init(spec, lats, lngs).ok());
}

// ---- Predicate Spec Parsing Tests ----

TEST_F(SearchPredicateEvaluatorTest, test_predicate_spec_from_params_radius) {
    std::map<std::string, std::string> params;
    params[AcornPredicateSpec::kPredicateType] = "radius";
    params[AcornPredicateSpec::kCenterLat] = "37.7749";
    params[AcornPredicateSpec::kCenterLng] = "-122.4194";
    params[AcornPredicateSpec::kRadiusM] = "5000";
    params[AcornPredicateSpec::kLatColumn] = "lat";
    params[AcornPredicateSpec::kLngColumn] = "lng";

    auto spec = AcornPredicateSpec::from_query_params(params);
    ASSERT_EQ(spec.type, AcornPredicateSpec::RADIUS);
    ASSERT_DOUBLE_EQ(spec.center_lat, 37.7749);
    ASSERT_DOUBLE_EQ(spec.center_lng, -122.4194);
    ASSERT_DOUBLE_EQ(spec.radius_meters, 5000.0);
    ASSERT_EQ(spec.lat_column_name, "lat");
    ASSERT_EQ(spec.lng_column_name, "lng");
}

TEST_F(SearchPredicateEvaluatorTest, test_predicate_spec_from_params_polygon) {
    std::map<std::string, std::string> params;
    params[AcornPredicateSpec::kPredicateType] = "polygon";
    params[AcornPredicateSpec::kWkt] = "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))";

    auto spec = AcornPredicateSpec::from_query_params(params);
    ASSERT_EQ(spec.type, AcornPredicateSpec::POLYGON);
    ASSERT_EQ(spec.wkt, "POLYGON((0 0, 1 0, 1 1, 0 1, 0 0))");
}

TEST_F(SearchPredicateEvaluatorTest, test_predicate_spec_from_params_none) {
    std::map<std::string, std::string> params;
    auto spec = AcornPredicateSpec::from_query_params(params);
    ASSERT_EQ(spec.type, AcornPredicateSpec::NONE);
}

// ---- Factory Tests ----

TEST_F(SearchPredicateEvaluatorTest, test_factory_radius) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    auto eval = create_predicate_evaluator(spec);
    ASSERT_NE(eval, nullptr);
}

TEST_F(SearchPredicateEvaluatorTest, test_factory_polygon) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    auto eval = create_predicate_evaluator(spec);
    ASSERT_NE(eval, nullptr);
}

TEST_F(SearchPredicateEvaluatorTest, test_factory_none) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::NONE;
    auto eval = create_predicate_evaluator(spec);
    ASSERT_EQ(eval, nullptr);
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_exact_boundary) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = 0.0;
    spec.center_lng = 0.0;
    spec.radius_meters = 111195; // ~1 degree latitude in meters

    std::vector<double> lats = {0.0, 1.0, 1.1};
    std::vector<double> lngs = {0.0, 0.0, 0.0};

    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_TRUE(eval.evaluate(0));  // at center
    ASSERT_TRUE(eval.evaluate(1));  // ~111km, just at boundary
    ASSERT_FALSE(eval.evaluate(2)); // beyond boundary
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_zero_radius) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 0;

    std::vector<double> lats = {kSFLat};
    std::vector<double> lngs = {kSFLng};

    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_FALSE(eval.evaluate(0)); // strict less-than: 0 < 0 is false
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_empty_data) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;
    spec.center_lat = kSFLat;
    spec.center_lng = kSFLng;
    spec.radius_meters = 5000;

    std::vector<double> lats, lngs;
    SpatialRadiusEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_EQ(eval.num_rows(), 0);
    ASSERT_FALSE(eval.evaluate(0)); // no data
}

TEST_F(SearchPredicateEvaluatorTest, test_radius_evaluator_wrong_type) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;

    std::vector<double> lats = {kSFLat};
    std::vector<double> lngs = {kSFLng};

    SpatialRadiusEvaluator eval;
    ASSERT_FALSE(eval.init(spec, lats, lngs).ok());
}

TEST_F(SearchPredicateEvaluatorTest, test_polygon_evaluator_multiple_points) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    spec.wkt = "POLYGON((-122.5 37.7, -122.3 37.7, -122.3 37.85, -122.5 37.85, -122.5 37.7))";

    std::vector<double> lats = {kSFLat, kFarLat, 37.75, 37.84};
    std::vector<double> lngs = {kSFLng, kFarLng, -122.4, -122.4};

    SpatialPolygonEvaluator eval;
    ASSERT_TRUE(eval.init(spec, lats, lngs).ok());
    ASSERT_TRUE(eval.evaluate(0));   // SF center: inside
    ASSERT_FALSE(eval.evaluate(1));  // San Jose: outside
    ASSERT_TRUE(eval.evaluate(2));   // inside box
    ASSERT_TRUE(eval.evaluate(3));   // inside box
}

TEST_F(SearchPredicateEvaluatorTest, test_polygon_evaluator_empty_wkt) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::POLYGON;
    spec.wkt = "";

    std::vector<double> lats = {kSFLat};
    std::vector<double> lngs = {kSFLng};

    SpatialPolygonEvaluator eval;
    ASSERT_FALSE(eval.init(spec, lats, lngs).ok());
}

TEST_F(SearchPredicateEvaluatorTest, test_polygon_evaluator_wrong_type) {
    AcornPredicateSpec spec;
    spec.type = AcornPredicateSpec::RADIUS;

    std::vector<double> lats = {kSFLat};
    std::vector<double> lngs = {kSFLng};

    SpatialPolygonEvaluator eval;
    ASSERT_FALSE(eval.init(spec, lats, lngs).ok());
}

TEST_F(SearchPredicateEvaluatorTest, test_predicate_spec_from_params_missing_type) {
    std::map<std::string, std::string> params;
    params[AcornPredicateSpec::kCenterLat] = "37.7749";
    params[AcornPredicateSpec::kCenterLng] = "-122.4194";

    auto spec = AcornPredicateSpec::from_query_params(params);
    ASSERT_EQ(spec.type, AcornPredicateSpec::NONE);
}

TEST_F(SearchPredicateEvaluatorTest, test_predicate_spec_from_params_unknown_type) {
    std::map<std::string, std::string> params;
    params[AcornPredicateSpec::kPredicateType] = "unknown_type";

    auto spec = AcornPredicateSpec::from_query_params(params);
    ASSERT_EQ(spec.type, AcornPredicateSpec::NONE);
}

TEST_F(SearchPredicateEvaluatorTest, test_predicate_spec_radius_missing_fields) {
    std::map<std::string, std::string> params;
    params[AcornPredicateSpec::kPredicateType] = "radius";

    auto spec = AcornPredicateSpec::from_query_params(params);
    ASSERT_EQ(spec.type, AcornPredicateSpec::RADIUS);
    ASSERT_DOUBLE_EQ(spec.center_lat, 0.0);
    ASSERT_DOUBLE_EQ(spec.center_lng, 0.0);
    ASSERT_DOUBLE_EQ(spec.radius_meters, 0.0);
}

} // namespace starrocks
