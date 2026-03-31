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

#include <cstdint>
#include <string>
#include <vector>

class S2Region;

namespace starrocks {

// S2 cell levels range from 0 (face, ~85M km²) to 30 (sub-cm).
// Useful reference:
//   Level 12 ≈ 3.3 km² (city-district scale)
//   Level 14 ≈ 0.2 km² (neighborhood scale)
//   Level 16 ≈ 0.01 km² (city-block scale)
constexpr int kS2MinLevel = 0;
constexpr int kS2MaxLevel = 30;
constexpr int kS2DefaultPartitionLevel = 12;

// Compute the S2 cell ID (as uint64) for a given lat/lng at a fixed S2 level.
// The cell ID is the *parent* cell at the requested level, suitable for
// partitioning points into spatial buckets.
// Returns 0 if the coordinates are invalid (|lng| > 180 or |lat| > 90).
uint64_t s2_cell_id_from_latlng(double lat_degrees, double lng_degrees, int level);

// Convert an S2 cell ID to a compact human-readable token string.
// Used for file naming: e.g. "3/210102" → "89c25c".
std::string s2_cell_id_to_token(uint64_t cell_id);

// Parse a token string back to a cell ID. Returns 0 on invalid input.
uint64_t s2_cell_id_from_token(const std::string& token);

// Return the set of S2 cell IDs at `level` that cover the given S2Cap
// (circle on sphere). Used at query time to determine which partitions
// to search for a spatial predicate like st_contains(st_circle(...)).
// `max_cells` controls the covering precision vs fan-out trade-off.
std::vector<uint64_t> s2_covering_cell_ids_for_cap(double center_lat_degrees, double center_lng_degrees,
                                                    double radius_meters, int level, int max_cells = 8);

// Return the set of S2 cell IDs at `level` that cover an arbitrary S2Region.
// This is the general form used when the query predicate is a polygon or
// other complex region.
std::vector<uint64_t> s2_covering_cell_ids(const S2Region& region, int level, int max_cells = 8);

// Return the set of S2 cell IDs at `level` that cover the polygon described
// by a WKT string (e.g. "POLYGON((lng1 lat1, lng2 lat2, ...))").
// Returns an empty vector if the WKT is invalid or not a polygon.
// This function is safe to call from code that cannot include S2 headers
// directly (e.g. segment_iterator.cpp).
std::vector<uint64_t> s2_covering_cell_ids_for_polygon_wkt(const std::string& wkt, int level, int max_cells = 8);

// G2: Expand a set of cell IDs by adding the 4 edge-neighbor cells of each
// input cell. Returns the deduplicated union of original + neighbor cells.
// Used at query time to catch vectors near cell boundaries.
std::vector<uint64_t> s2_expand_with_neighbors(const std::vector<uint64_t>& cell_ids);

// Validate that an S2 level is within [0, 30].
inline bool s2_is_valid_level(int level) {
    return level >= kS2MinLevel && level <= kS2MaxLevel;
}

} // namespace starrocks
