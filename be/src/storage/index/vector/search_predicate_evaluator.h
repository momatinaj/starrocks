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

#pragma once

#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "common/status.h"

namespace starrocks {

// Describes the spatial predicate pushed down from FE for ACORN-1 search.
struct AcornPredicateSpec {
    enum Type { NONE, RADIUS, POLYGON };

    Type type = NONE;

    // For RADIUS: center point and radius in meters
    double center_lat = 0.0;
    double center_lng = 0.0;
    double radius_meters = 0.0;

    // For POLYGON: WKT representation
    std::string wkt;

    // Column names (used to identify which segment columns to pre-load)
    std::string lat_column_name;
    std::string lng_column_name;

    // Query param keys used in VectorSearchOptions.query_params
    static constexpr const char* kPredicateType = "acorn_predicate_type";
    static constexpr const char* kCenterLat = "acorn_predicate_center_lat";
    static constexpr const char* kCenterLng = "acorn_predicate_center_lng";
    static constexpr const char* kRadiusM = "acorn_predicate_radius_m";
    static constexpr const char* kWkt = "acorn_predicate_wkt";
    static constexpr const char* kLatColumn = "acorn_lat_column";
    static constexpr const char* kLngColumn = "acorn_lng_column";

    // Parse from query_params map
    static AcornPredicateSpec from_query_params(const std::map<std::string, std::string>& params);
};

// Interface for evaluating predicates by row ID during ACORN-1 graph traversal.
class SearchPredicateEvaluator {
public:
    virtual ~SearchPredicateEvaluator() = default;

    virtual Status init(const AcornPredicateSpec& spec, const std::vector<double>& lat_data,
                        const std::vector<double>& lng_data) = 0;

    // Returns true if the row at the given ID satisfies the predicate.
    virtual bool evaluate(int64_t row_id) const = 0;

    // Returns the number of rows in the pre-loaded data.
    virtual size_t num_rows() const = 0;
};

// Evaluates: ST_Distance_Sphere(lat[row_id], lng[row_id], center_lat, center_lng) < radius_meters
class SpatialRadiusEvaluator : public SearchPredicateEvaluator {
public:
    SpatialRadiusEvaluator() = default;

    Status init(const AcornPredicateSpec& spec, const std::vector<double>& lat_data,
                const std::vector<double>& lng_data) override;

    bool evaluate(int64_t row_id) const override;

    size_t num_rows() const override { return _lat_data.size(); }

private:
    static constexpr double kEarthRadiusMeters = 6371000.0;

    // Haversine distance in meters between two lat/lng points (in degrees).
    static double haversine_meters(double lat1_deg, double lng1_deg, double lat2_deg, double lng2_deg);

    std::vector<double> _lat_data;
    std::vector<double> _lng_data;
    double _center_lat = 0.0;
    double _center_lng = 0.0;
    double _radius_meters = 0.0;
};

// Evaluates: ST_Contains(polygon, ST_Point(lng[row_id], lat[row_id]))
// Uses a simple point-in-polygon test via ray casting.
class SpatialPolygonEvaluator : public SearchPredicateEvaluator {
public:
    SpatialPolygonEvaluator() = default;

    Status init(const AcornPredicateSpec& spec, const std::vector<double>& lat_data,
                const std::vector<double>& lng_data) override;

    bool evaluate(int64_t row_id) const override;

    size_t num_rows() const override { return _lat_data.size(); }

private:
    struct Point {
        double x, y;
    };

    // Parse WKT POLYGON string into a vector of points.
    static Status parse_wkt_polygon(const std::string& wkt, std::vector<Point>& ring);

    // Ray-casting point-in-polygon test.
    static bool point_in_polygon(double x, double y, const std::vector<Point>& ring);

    std::vector<double> _lat_data;
    std::vector<double> _lng_data;
    std::vector<Point> _polygon_ring;
};

// Factory to create the appropriate evaluator from a predicate spec.
std::unique_ptr<SearchPredicateEvaluator> create_predicate_evaluator(const AcornPredicateSpec& spec);

} // namespace starrocks
