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

#include <s2/s1angle.h>
#include <s2/s2cap.h>
#include <s2/s2cell_id.h>
#include <s2/s2earth.h>
#include <s2/s2latlng.h>
#include <s2/s2polygon.h>
#include <s2/s2region_coverer.h>

#include <algorithm>
#include <cmath>
#include <set>

#include "geo/geo_types.h"

namespace starrocks {

uint64_t s2_cell_id_from_latlng(double lat_degrees, double lng_degrees, int level) {
    if (std::abs(lng_degrees) > 180 || std::abs(lat_degrees) > 90) {
        return 0;
    }
    if (!s2_is_valid_level(level)) {
        return 0;
    }
    S2LatLng ll = S2LatLng::FromDegrees(lat_degrees, lng_degrees);
    S2CellId cell_id(ll.ToPoint());
    return cell_id.parent(level).id();
}

std::string s2_cell_id_to_token(uint64_t cell_id) {
    return S2CellId(cell_id).ToToken();
}

uint64_t s2_cell_id_from_token(const std::string& token) {
    S2CellId cell_id = S2CellId::FromToken(token);
    if (!cell_id.is_valid()) {
        return 0;
    }
    return cell_id.id();
}

std::vector<uint64_t> s2_covering_cell_ids_for_cap(double center_lat_degrees, double center_lng_degrees,
                                                    double radius_meters, int level, int max_cells) {
    if (std::abs(center_lng_degrees) > 180 || std::abs(center_lat_degrees) > 90) {
        return {};
    }
    if (!s2_is_valid_level(level) || radius_meters < 0) {
        return {};
    }

    S2LatLng center = S2LatLng::FromDegrees(center_lat_degrees, center_lng_degrees);
    S1Angle radius = S2Earth::ToAngle(util::units::Meters(radius_meters));
    S2Cap cap(center.ToPoint(), radius);

    return s2_covering_cell_ids(cap, level, max_cells);
}

std::vector<uint64_t> s2_covering_cell_ids(const S2Region& region, int level, int max_cells) {
    if (!s2_is_valid_level(level)) {
        return {};
    }

    S2RegionCoverer::Options options;
    options.set_fixed_level(level);
    options.set_max_cells(max_cells);
    S2RegionCoverer coverer(options);

    std::vector<S2CellId> covering;
    coverer.GetCovering(region, &covering);

    std::vector<uint64_t> result;
    result.reserve(covering.size());
    for (const auto& cell_id : covering) {
        result.push_back(cell_id.id());
    }
    return result;
}

std::vector<uint64_t> s2_covering_cell_ids_for_polygon_wkt(const std::string& wkt, int level, int max_cells) {
    if (wkt.empty() || !s2_is_valid_level(level)) {
        return {};
    }

    GeoParseStatus status;
    std::unique_ptr<GeoShape> shape(GeoShape::from_wkt(wkt.data(), wkt.size(), &status));
    if (!shape || shape->type() != GEO_SHAPE_POLYGON) {
        return {};
    }

    auto* geo_polygon = static_cast<GeoPolygon*>(shape.get());
    const S2Polygon* s2_poly = geo_polygon->polygon();
    if (!s2_poly) {
        return {};
    }

    return s2_covering_cell_ids(*s2_poly, level, max_cells);
}

std::vector<uint64_t> s2_expand_with_neighbors(const std::vector<uint64_t>& cell_ids) {
    std::set<uint64_t> expanded(cell_ids.begin(), cell_ids.end());

    for (uint64_t id : cell_ids) {
        S2CellId cell(id);
        if (!cell.is_valid() || cell.is_face()) continue;

        S2CellId neighbors[4];
        cell.GetEdgeNeighbors(neighbors);
        for (const auto& n : neighbors) {
            if (n.is_valid()) {
                expanded.insert(n.id());
            }
        }
    }

    return {expanded.begin(), expanded.end()};
}

} // namespace starrocks
