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

package com.starrocks.common;

import com.starrocks.thrift.TVectorSearchOptions;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

public class VectorSearchOptions {
    private static final int RESULT_ORDER_ASC = 0;
    private static final int RESULT_ORDER_DESC = 1;
    public static final String QUERY_PARAM_FALLBACK_MODE = "vector_fallback_mode";

    public enum FallbackMode {
        NONE("NONE"),
        SPATIAL_FILTER_EXACT("SPATIAL_FILTER + EXACT_DISTANCE");

        private final String explainName;

        FallbackMode(String explainName) {
            this.explainName = explainName;
        }

        public String getExplainName() {
            return explainName;
        }
    }

    public enum AcornPredicateType {
        NONE, RADIUS, POLYGON
    }

    private boolean enableUseANN = false;
    private boolean useIVFPQ = false;
    private boolean useAcorn = false;
    private boolean useAcornGamma = false;
    private int acornGamma = 2;
    private boolean useGridHnsw = false;
    private FallbackMode fallbackMode = FallbackMode.NONE;

    private String distanceColumnName = "";
    private int distanceSlotId = 0;

    private long limitK = 0;
    private int resultOrder = 0;

    private double predicateRange = -1;
    private List<String> queryVector = new ArrayList<>();

    private AcornPredicateType acornPredicateType = AcornPredicateType.NONE;
    private double acornCenterLat = 0.0;
    private double acornCenterLng = 0.0;
    private double acornRadiusMeters = 0.0;
    private String acornWkt = "";
    private String acornLatColumn = "";
    private String acornLngColumn = "";

    public boolean isEnableUseANN() {
        return enableUseANN;
    }

    public void setEnableUseANN(boolean enableUseANN) {
        this.enableUseANN = enableUseANN;
    }

    public boolean isUseIVFPQ() {
        return useIVFPQ;
    }

    public void setUseIVFPQ(boolean useIVFPQ) {
        this.useIVFPQ = useIVFPQ;
    }

    public boolean isUseFallback() {
        return fallbackMode != FallbackMode.NONE;
    }

    public void setFallbackMode(FallbackMode fallbackMode) {
        this.fallbackMode = fallbackMode;
    }

    public String getDistanceColumnName() {
        return distanceColumnName;
    }

    public void setDistanceColumnName(String distanceColumnName) {
        this.distanceColumnName = distanceColumnName;
    }

    public void setDistanceSlotId(int distanceSlotId) {
        this.distanceSlotId = distanceSlotId;
    }

    public void setLimitK(long limitK) {
        this.limitK = limitK;
    }

    public void setQueryVector(List<String> queryVector) {
        this.queryVector = queryVector;
    }

    public void setPredicateRange(double predicateRange) {
        this.predicateRange = predicateRange;
    }

    public void setResultOrder(boolean isAsc) {
        this.resultOrder = isAsc ? RESULT_ORDER_ASC : RESULT_ORDER_DESC;
    }

    public boolean isUseAcorn() {
        return useAcorn;
    }

    public void setUseAcorn(boolean useAcorn) {
        this.useAcorn = useAcorn;
    }

    public boolean isUseAcornGamma() {
        return useAcornGamma;
    }

    public void setUseAcornGamma(boolean useAcornGamma) {
        this.useAcornGamma = useAcornGamma;
    }

    public int getAcornGamma() {
        return acornGamma;
    }

    public void setAcornGamma(int acornGamma) {
        this.acornGamma = acornGamma;
    }

    public boolean isUseGridHnsw() {
        return useGridHnsw;
    }

    public void setUseGridHnsw(boolean useGridHnsw) {
        this.useGridHnsw = useGridHnsw;
    }

    public void setAcornRadiusPredicate(double centerLat, double centerLng, double radiusMeters,
                                        String latColumn, String lngColumn) {
        this.acornPredicateType = AcornPredicateType.RADIUS;
        this.acornCenterLat = centerLat;
        this.acornCenterLng = centerLng;
        this.acornRadiusMeters = radiusMeters;
        this.acornLatColumn = latColumn;
        this.acornLngColumn = lngColumn;
    }

    public void setAcornPolygonPredicate(String wkt, String latColumn, String lngColumn) {
        this.acornPredicateType = AcornPredicateType.POLYGON;
        this.acornWkt = wkt;
        this.acornLatColumn = latColumn;
        this.acornLngColumn = lngColumn;
    }

    public AcornPredicateType getAcornPredicateType() {
        return acornPredicateType;
    }

    public TVectorSearchOptions toThrift() {
        TVectorSearchOptions opts = new TVectorSearchOptions();
        opts.setEnable_use_ann(enableUseANN);
        opts.setVector_limit_k(limitK);
        opts.setVector_distance_column_name(distanceColumnName);
        opts.setVector_slot_id(distanceSlotId);
        opts.setQuery_vector(queryVector);
        opts.setVector_range(predicateRange);
        opts.setResult_order(resultOrder);
        opts.setUse_ivfpq(useIVFPQ);

        Map<String, String> queryParams = new HashMap<>();
        if (isUseFallback()) {
            queryParams.put(QUERY_PARAM_FALLBACK_MODE, fallbackMode.name());
        }
        if (useAcorn) {
            queryParams.put("index_type", "acorn");
            if (acornPredicateType != AcornPredicateType.NONE) {
                queryParams.put("acorn_predicate_type", acornPredicateType.name());
                queryParams.put("acorn_lat_column", acornLatColumn);
                queryParams.put("acorn_lng_column", acornLngColumn);
                if (acornPredicateType == AcornPredicateType.RADIUS) {
                    queryParams.put("acorn_predicate_center_lat", String.valueOf(acornCenterLat));
                    queryParams.put("acorn_predicate_center_lng", String.valueOf(acornCenterLng));
                    queryParams.put("acorn_predicate_radius_m", String.valueOf(acornRadiusMeters));
                } else if (acornPredicateType == AcornPredicateType.POLYGON) {
                    queryParams.put("acorn_predicate_wkt", acornWkt);
                }
            }
        }
        if (useAcornGamma) {
            queryParams.put("index_type", "acorn_gamma");
            queryParams.put("acorn_gamma", String.valueOf(acornGamma));
            if (acornPredicateType != AcornPredicateType.NONE) {
                queryParams.put("acorn_predicate_type", acornPredicateType.name());
                queryParams.put("acorn_lat_column", acornLatColumn);
                queryParams.put("acorn_lng_column", acornLngColumn);
                if (acornPredicateType == AcornPredicateType.RADIUS) {
                    queryParams.put("acorn_predicate_center_lat", String.valueOf(acornCenterLat));
                    queryParams.put("acorn_predicate_center_lng", String.valueOf(acornCenterLng));
                    queryParams.put("acorn_predicate_radius_m", String.valueOf(acornRadiusMeters));
                } else if (acornPredicateType == AcornPredicateType.POLYGON) {
                    queryParams.put("acorn_predicate_wkt", acornWkt);
                }
            }
        }
        if (useGridHnsw) {
            queryParams.put("index_type", "grid_hnsw");
            if (acornPredicateType != AcornPredicateType.NONE) {
                queryParams.put("grid_predicate_type", acornPredicateType.name());
                queryParams.put("grid_lat_column", acornLatColumn);
                queryParams.put("grid_lng_column", acornLngColumn);
                if (acornPredicateType == AcornPredicateType.RADIUS) {
                    queryParams.put("grid_predicate_center_lat", String.valueOf(acornCenterLat));
                    queryParams.put("grid_predicate_center_lng", String.valueOf(acornCenterLng));
                    queryParams.put("grid_predicate_radius_m", String.valueOf(acornRadiusMeters));
                } else if (acornPredicateType == AcornPredicateType.POLYGON) {
                    queryParams.put("grid_predicate_wkt", acornWkt);
                }
            }
        }
        if (!queryParams.isEmpty()) {
            opts.setQuery_params(queryParams);
        }
        return opts;
    }

    public String getExplainString(String prefix) {
        if (isUseFallback()) {
            return prefix + "VECTORINDEX: FALLBACK" + "\n" +
                    prefix + prefix + "Fallback Mode: " + fallbackMode.getExplainName() + "\n";
        }
        StringBuilder sb = new StringBuilder();
        sb.append(prefix).append("VECTORINDEX: ON").append("\n");
        sb.append(prefix).append(prefix);
        sb.append("IVFPQ: ").append(useIVFPQ ? "ON" : "OFF").append(", ");
        sb.append("Distance Column: <").append(distanceSlotId).append(":").append(distanceColumnName).append(">, ");
        sb.append("LimitK: ").append(limitK).append(", ");
        sb.append("Order: ").append(resultOrder == RESULT_ORDER_ASC ? "ASC" : "DESC").append(", ");
        sb.append("Query Vector: ").append(queryVector).append(", ");
        sb.append("Predicate Range: ").append(predicateRange);
        if (useAcorn) {
            sb.append(", ACORN: ON");
            if (acornPredicateType == AcornPredicateType.RADIUS) {
                sb.append(", Spatial Predicate: RADIUS(")
                        .append(acornCenterLat).append(", ")
                        .append(acornCenterLng).append(", ")
                        .append(acornRadiusMeters).append("m)");
            } else if (acornPredicateType == AcornPredicateType.POLYGON) {
                sb.append(", Spatial Predicate: POLYGON");
            }
        }
        if (useGridHnsw) {
            sb.append(", GRID_HNSW: ON");
            if (acornPredicateType == AcornPredicateType.RADIUS) {
                sb.append(", Spatial Predicate: RADIUS(")
                        .append(acornCenterLat).append(", ")
                        .append(acornCenterLng).append(", ")
                        .append(acornRadiusMeters).append("m)");
            } else if (acornPredicateType == AcornPredicateType.POLYGON) {
                sb.append(", Spatial Predicate: POLYGON");
            }
        }
        sb.append("\n");
        return sb.toString();
    }
}