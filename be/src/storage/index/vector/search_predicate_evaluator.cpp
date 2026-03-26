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

#include <cmath>
#include <regex>
#include <sstream>

namespace starrocks {

// ---- AcornPredicateSpec ----

AcornPredicateSpec AcornPredicateSpec::from_query_params(const std::map<std::string, std::string>& params) {
    AcornPredicateSpec spec;

    auto it = params.find(kPredicateType);
    if (it == params.end()) return spec;

    const auto& type_str = it->second;
    if (type_str == "radius") {
        spec.type = RADIUS;
        auto lat_it = params.find(kCenterLat);
        auto lng_it = params.find(kCenterLng);
        auto rad_it = params.find(kRadiusM);
        if (lat_it != params.end()) spec.center_lat = std::stod(lat_it->second);
        if (lng_it != params.end()) spec.center_lng = std::stod(lng_it->second);
        if (rad_it != params.end()) spec.radius_meters = std::stod(rad_it->second);
    } else if (type_str == "polygon") {
        spec.type = POLYGON;
        auto wkt_it = params.find(kWkt);
        if (wkt_it != params.end()) spec.wkt = wkt_it->second;
    }

    auto lat_col = params.find(kLatColumn);
    auto lng_col = params.find(kLngColumn);
    if (lat_col != params.end()) spec.lat_column_name = lat_col->second;
    if (lng_col != params.end()) spec.lng_column_name = lng_col->second;

    return spec;
}

// ---- SpatialRadiusEvaluator ----

double SpatialRadiusEvaluator::haversine_meters(double lat1_deg, double lng1_deg, double lat2_deg, double lng2_deg) {
    constexpr double kDegToRad = M_PI / 180.0;
    double lat1 = lat1_deg * kDegToRad;
    double lat2 = lat2_deg * kDegToRad;
    double dlat = (lat2_deg - lat1_deg) * kDegToRad;
    double dlng = (lng2_deg - lng1_deg) * kDegToRad;

    double a = std::sin(dlat / 2) * std::sin(dlat / 2) +
               std::cos(lat1) * std::cos(lat2) * std::sin(dlng / 2) * std::sin(dlng / 2);
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));
    return kEarthRadiusMeters * c;
}

Status SpatialRadiusEvaluator::init(const AcornPredicateSpec& spec, const std::vector<double>& lat_data,
                                    const std::vector<double>& lng_data) {
    if (spec.type != AcornPredicateSpec::RADIUS) {
        return Status::InvalidArgument("Expected RADIUS predicate");
    }
    if (lat_data.size() != lng_data.size()) {
        return Status::InvalidArgument("lat/lng data size mismatch");
    }
    _lat_data = lat_data;
    _lng_data = lng_data;
    _center_lat = spec.center_lat;
    _center_lng = spec.center_lng;
    _radius_meters = spec.radius_meters;
    return Status::OK();
}

bool SpatialRadiusEvaluator::evaluate(int64_t row_id) const {
    if (row_id < 0 || row_id >= static_cast<int64_t>(_lat_data.size())) return false;
    double dist = haversine_meters(_lat_data[row_id], _lng_data[row_id], _center_lat, _center_lng);
    return dist < _radius_meters;
}

// ---- SpatialPolygonEvaluator ----

Status SpatialPolygonEvaluator::parse_wkt_polygon(const std::string& wkt, std::vector<Point>& ring) {
    // Expected format: POLYGON((x1 y1, x2 y2, ..., x1 y1))
    std::string upper_wkt = wkt;
    for (auto& c : upper_wkt) c = std::toupper(c);

    auto paren_start = upper_wkt.find("((");
    auto paren_end = upper_wkt.rfind("))");
    if (paren_start == std::string::npos || paren_end == std::string::npos) {
        return Status::InvalidArgument("Invalid WKT polygon format");
    }

    std::string coords_str = wkt.substr(paren_start + 2, paren_end - paren_start - 2);

    std::istringstream ss(coords_str);
    std::string token;
    while (std::getline(ss, token, ',')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t");
        if (start == std::string::npos) continue;
        token = token.substr(start);

        double x, y;
        std::istringstream pair_ss(token);
        if (!(pair_ss >> x >> y)) {
            return Status::InvalidArgument("Failed to parse coordinate pair: " + token);
        }
        ring.push_back({x, y});
    }

    if (ring.size() < 4) {
        return Status::InvalidArgument("Polygon must have at least 4 points (including closing point)");
    }

    return Status::OK();
}

bool SpatialPolygonEvaluator::point_in_polygon(double x, double y, const std::vector<Point>& ring) {
    bool inside = false;
    size_t n = ring.size();
    for (size_t i = 0, j = n - 1; i < n; j = i++) {
        if (((ring[i].y > y) != (ring[j].y > y)) &&
            (x < (ring[j].x - ring[i].x) * (y - ring[i].y) / (ring[j].y - ring[i].y) + ring[i].x)) {
            inside = !inside;
        }
    }
    return inside;
}

Status SpatialPolygonEvaluator::init(const AcornPredicateSpec& spec, const std::vector<double>& lat_data,
                                     const std::vector<double>& lng_data) {
    if (spec.type != AcornPredicateSpec::POLYGON) {
        return Status::InvalidArgument("Expected POLYGON predicate");
    }
    if (lat_data.size() != lng_data.size()) {
        return Status::InvalidArgument("lat/lng data size mismatch");
    }
    _lat_data = lat_data;
    _lng_data = lng_data;
    RETURN_IF_ERROR(parse_wkt_polygon(spec.wkt, _polygon_ring));
    return Status::OK();
}

bool SpatialPolygonEvaluator::evaluate(int64_t row_id) const {
    if (row_id < 0 || row_id >= static_cast<int64_t>(_lat_data.size())) return false;
    // WKT POLYGON uses (lng, lat) = (x, y) convention
    return point_in_polygon(_lng_data[row_id], _lat_data[row_id], _polygon_ring);
}

// ---- Factory ----

std::unique_ptr<SearchPredicateEvaluator> create_predicate_evaluator(const AcornPredicateSpec& spec) {
    switch (spec.type) {
    case AcornPredicateSpec::RADIUS:
        return std::make_unique<SpatialRadiusEvaluator>();
    case AcornPredicateSpec::POLYGON:
        return std::make_unique<SpatialPolygonEvaluator>();
    default:
        return nullptr;
    }
}

} // namespace starrocks
