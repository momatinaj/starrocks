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

#include <map>
#include <string>
#include <vector>

#include "common/global_types.h"

namespace starrocks {

class BaseRowset;
class Segment;

struct VectorSearchOption {
public:
    static constexpr const char* kFallbackModeKey = "vector_fallback_mode";

    int64_t k;

    std::vector<float> query_vector;

    std::string vector_distance_column_name;

    bool use_vector_index = false;

    int vector_column_id;

    SlotId vector_slot_id;

    std::map<std::string, std::string> query_params;

    std::string fallback_mode;

    double vector_range;

    int result_order;

    bool use_ivfpq = false;

    double pq_refine_factor;

    double k_factor;

    bool use_vector_fallback() const { return !fallback_mode.empty(); }

    VectorSearchOption() = default;
};

} // namespace starrocks
