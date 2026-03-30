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

#ifdef WITH_TENANN

#include <cstdint>
#include <numeric>
#include <string>
#include <vector>

#include "tenann/common/seq_view.h"
#include "tenann/factory/index_factory.h"
#include "tenann/store/index_meta.h"
#include "tenann/store/index_type.h"

namespace starrocks::test {

// Build a real HNSW index using TenANN and write it to disk.
// This produces the same file format that production code generates.
// When with_idmap=true, TenANN wraps the index in IndexIDMap (same as
// when nullable vector columns are used in production).
inline std::string build_hnsw_index_file(const std::string& path, int dim, int M, int num_vectors,
                                         const std::vector<float>& vectors, bool with_idmap = false) {
    tenann::IndexMeta meta;
    meta.SetIndexFamily(tenann::IndexFamily::kVectorIndex);
    meta.SetIndexType(tenann::IndexType::kFaissHnsw);
    meta.common_params()["dim"] = dim;
    meta.common_params()["metric_type"] = tenann::MetricType::kL2Distance;
    meta.index_params()["M"] = M;
    meta.index_params()["efConstruction"] = 40;
    meta.search_params()["efSearch"] = 40;
    auto builder = tenann::IndexFactory::CreateBuilderFromMeta(meta);
    if (with_idmap) {
        builder->EnableCustomRowId();
    }
    builder->Open(path);

    tenann::ArraySeqView view{.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(vectors.data())),
                              .dim = static_cast<uint32_t>(dim),
                              .size = static_cast<uint32_t>(num_vectors),
                              .elem_type = tenann::kFloatType};

    std::vector<int64_t> row_ids(num_vectors);
    std::iota(row_ids.begin(), row_ids.end(), 0);

    builder->Add({view}, row_ids.data(), nullptr);
    builder->Flush();
    builder->Close();

    return path;
}

// Same as above but with custom external IDs (for testing IDMap).
inline std::string build_hnsw_index_file_with_ids(const std::string& path, int dim, int M, int num_vectors,
                                                   const std::vector<float>& vectors,
                                                   const std::vector<int64_t>& external_ids) {
    tenann::IndexMeta meta;
    meta.SetIndexFamily(tenann::IndexFamily::kVectorIndex);
    meta.SetIndexType(tenann::IndexType::kFaissHnsw);
    meta.common_params()["dim"] = dim;
    meta.common_params()["metric_type"] = tenann::MetricType::kL2Distance;
    meta.index_params()["M"] = M;
    meta.index_params()["efConstruction"] = 40;
    meta.search_params()["efSearch"] = 40;
    auto builder = tenann::IndexFactory::CreateBuilderFromMeta(meta);
    builder->EnableCustomRowId();
    builder->Open(path);

    tenann::ArraySeqView view{.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(vectors.data())),
                              .dim = static_cast<uint32_t>(dim),
                              .size = static_cast<uint32_t>(num_vectors),
                              .elem_type = tenann::kFloatType};

    builder->Add({view}, external_ids.data(), nullptr);
    builder->Flush();
    builder->Close();

    return path;
}

} // namespace starrocks::test

#endif
