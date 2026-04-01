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
package com.starrocks.sql.optimizer.rule.transformation;

import com.google.common.base.Enums;
import com.google.common.base.Preconditions;
import com.starrocks.analysis.BinaryType;
import com.starrocks.catalog.ArrayType;
import com.starrocks.catalog.Column;
import com.starrocks.catalog.ColumnId;
import com.starrocks.catalog.Index;
import com.starrocks.catalog.OlapTable;
import com.starrocks.catalog.Type;
import com.starrocks.common.Config;
import com.starrocks.common.VectorIndexParams;
import com.starrocks.common.VectorSearchOptions;
import com.starrocks.qe.SessionVariable;
import com.starrocks.sql.analyzer.SemanticException;
import com.starrocks.sql.ast.IndexDef;
import com.starrocks.sql.optimizer.OptExpression;
import com.starrocks.sql.optimizer.OptimizerContext;
import com.starrocks.sql.optimizer.operator.OperatorType;
import com.starrocks.sql.optimizer.operator.Projection;
import com.starrocks.sql.optimizer.operator.logical.LogicalOlapScanOperator;
import com.starrocks.sql.optimizer.operator.logical.LogicalTopNOperator;
import com.starrocks.sql.optimizer.operator.pattern.Pattern;
import com.starrocks.sql.optimizer.operator.scalar.ArrayOperator;
import com.starrocks.sql.optimizer.operator.scalar.BinaryPredicateOperator;
import com.starrocks.sql.optimizer.operator.scalar.CallOperator;
import com.starrocks.sql.optimizer.operator.scalar.CastOperator;
import com.starrocks.sql.optimizer.operator.scalar.ColumnRefOperator;
import com.starrocks.sql.optimizer.operator.scalar.CompoundPredicateOperator;
import com.starrocks.sql.optimizer.operator.scalar.ConstantOperator;
import com.starrocks.sql.optimizer.operator.scalar.ScalarOperator;
import com.starrocks.sql.optimizer.rule.RuleType;
import org.apache.commons.lang3.StringUtils;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.stream.Collectors;

import static com.starrocks.analysis.BinaryType.GE;
import static com.starrocks.analysis.BinaryType.LE;
import static com.starrocks.catalog.FunctionSet.APPROX_COSINE_SIMILARITY;
import static com.starrocks.catalog.FunctionSet.APPROX_L2_DISTANCE;
import static com.starrocks.catalog.FunctionSet.ST_CONTAINS;
import static com.starrocks.catalog.FunctionSet.ST_DISTANCE_SPHERE;

public class RewriteToVectorPlanRule extends TransformationRule {

    public RewriteToVectorPlanRule() {
        super(RuleType.TF_VECTOR_REWRITE_RULE,
                Pattern.create(OperatorType.LOGICAL_TOPN)
                        .addChildren(Pattern.create(OperatorType.LOGICAL_OLAP_SCAN)));
    }

    @Override
    public boolean check(OptExpression input, OptimizerContext context) {
        if (!Config.enable_experimental_vector) {
            return false;
        }

        LogicalTopNOperator topNOp = (LogicalTopNOperator) input.getOp();
        LogicalOlapScanOperator scanOp = (LogicalOlapScanOperator) input.getInputs().get(0).getOp();

        if (scanOp.getProjection() == null) {
            return false;
        }

        if (topNOp.getLimit() <= 0 || topNOp.getOrderByElements().size() != 1) {
            return false;
        }

        return true;
    }

    @Override
    public List<OptExpression> transform(OptExpression input, OptimizerContext context) {
        LogicalTopNOperator topNOp = (LogicalTopNOperator) input.getOp();
        LogicalOlapScanOperator scanOp = (LogicalOlapScanOperator) input.getInputs().get(0).getOp();
        VectorSearchOptions opts = scanOp.getVectorSearchOptions();

        Optional<VectorFuncInfo> optionalInfo = extractOrderByVectorFuncInfo(topNOp, scanOp);
        if (optionalInfo.isEmpty()) {
            return List.of();
        }
        VectorFuncInfo info = optionalInfo.get();

        int dim =
                Integer.parseInt(info.index.getProperties().get(VectorIndexParams.CommonIndexParamKey.DIM.name().toLowerCase()));
        if (info.vectorQuery.size() != dim) {
            throw new SemanticException(
                    String.format("The vector query size (%s) is not equal to the vector index dimension (%d)",
                            info.vectorQuery, dim));
        }

        opts.setEnableUseANN(false);
        opts.setUseIVFPQ(false);
        opts.setFallbackMode(VectorSearchOptions.FallbackMode.NONE);
        opts.setPredicateRange(-1);
        opts.setLimitK(topNOp.getLimit());
        opts.setResultOrder(info.isAscending);
        opts.setQueryVector(info.vectorQuery);

        String indexType = info.index.getProperties().get(VectorIndexParams.CommonIndexParamKey.INDEX_TYPE.name().toLowerCase());
        boolean isAcorn = VectorIndexParams.VectorIndexType.ACORN.name().equalsIgnoreCase(indexType);
        boolean isGridHnsw = VectorIndexParams.VectorIndexType.GRID_HNSW.name().equalsIgnoreCase(indexType);
        boolean isAcornGamma = VectorIndexParams.VectorIndexType.ACORN_GAMMA.name().equalsIgnoreCase(indexType);

        ScalarOperator predicate = scanOp.getPredicate();
        if (predicate != null) {
            if (containsSupportedSpatialPredicate(predicate)) {
                if (isAcorn) {
                    opts.setUseAcorn(true);
                    extractAcornSpatialParams(predicate, scanOp, opts);
                    opts.setEnableUseANN(true);
                    opts.setDistanceColumnName("__vector_" + info.outColumnRef.getName());
                    return List.of(rewriteOptByDistanceColumn(topNOp, scanOp, context, predicate, info, opts));
                }
                if (isAcornGamma) {
                    opts.setUseAcornGamma(true);
                    String gamma = info.index.getProperties().getOrDefault("gamma", "2");
                    opts.setAcornGamma(Integer.parseInt(gamma));
                    extractAcornSpatialParams(predicate, scanOp, opts);
                    opts.setEnableUseANN(true);
                    opts.setDistanceColumnName("__vector_" + info.outColumnRef.getName());
                    return List.of(rewriteOptByDistanceColumn(topNOp, scanOp, context, predicate, info, opts));
                }
                if (isGridHnsw) {
                    opts.setUseGridHnsw(true);
                    applyGridImprovementParams(context, info.index.getProperties(), opts);
                    extractAcornSpatialParams(predicate, scanOp, opts);
                    opts.setEnableUseANN(true);
                    opts.setDistanceColumnName("__vector_" + info.outColumnRef.getName());
                    return List.of(rewriteOptByDistanceColumn(topNOp, scanOp, context, predicate, info, opts));
                }
                opts.setFallbackMode(VectorSearchOptions.FallbackMode.SPATIAL_FILTER_EXACT);
                LogicalOlapScanOperator newScanOp = LogicalOlapScanOperator.builder()
                        .withOperator(scanOp)
                        .build();
                return List.of(OptExpression.create(topNOp, OptExpression.create(newScanOp)));
            }
            Optional<Double> value = extractVectorRange(predicate, info);
            if (value.isEmpty()) {
                return List.of();
            }
            predicate = null;
            opts.setPredicateRange(value.get());
        }

        opts.setEnableUseANN(true);
        if (isAcorn) {
            opts.setUseAcorn(true);
        }
        if (isAcornGamma) {
            opts.setUseAcornGamma(true);
            String gamma = info.index.getProperties().getOrDefault("gamma", "2");
            opts.setAcornGamma(Integer.parseInt(gamma));
        }
        if (isGridHnsw) {
            opts.setUseGridHnsw(true);
            applyGridImprovementParams(context, info.index.getProperties(), opts);
        }
        opts.setUseIVFPQ(VectorIndexParams.VectorIndexType.IVFPQ.name().equalsIgnoreCase(indexType));
        opts.setDistanceColumnName("__vector_" + info.outColumnRef.getName());

        if (opts.isUseIVFPQ()) {
            // Skip rewrite because IVFPQ is inaccurate and requires a brute force search after the ANN index search
            LogicalOlapScanOperator newScanOp = LogicalOlapScanOperator.builder()
                    .withOperator(scanOp)
                    .setPredicate(predicate)
                    .build();
            return List.of(OptExpression.create(topNOp, OptExpression.create(newScanOp)));
        }

        return List.of(rewriteOptByDistanceColumn(topNOp, scanOp, context, predicate, info, opts));
    }

    private OptExpression rewriteOptByDistanceColumn(LogicalTopNOperator topNOp,
                                                     LogicalOlapScanOperator scanOp,
                                                     OptimizerContext context,
                                                     ScalarOperator newPredicate,
                                                     VectorFuncInfo info,
                                                     VectorSearchOptions opts) {
        // Add index distanceColumn to the scan operator, including table, colRefToColumnMetaMap, and columnMetaToColRefMap.
        String distanceColumnName = scanOp.getVectorSearchOptions().getDistanceColumnName();
        Column distanceColumn = new Column(distanceColumnName, Type.FLOAT);
        scanOp.getTable().addColumn(distanceColumn);

        ColumnRefOperator distanceColRef = context.getColumnRefFactory().create(distanceColumnName, Type.FLOAT, false);
        Map<ColumnRefOperator, Column> newColRefToColumnMetaMap = new HashMap<>(scanOp.getColRefToColumnMetaMap());
        newColRefToColumnMetaMap.put(distanceColRef, distanceColumn);

        Map<Column, ColumnRefOperator> newColumnMetaToColRefMap = new HashMap<>(scanOp.getColumnMetaToColRefMap());
        newColumnMetaToColRefMap.put(distanceColumn, distanceColRef);

        opts.setDistanceSlotId(distanceColRef.getId());

        // Replace the original function call by the distance column ref.
        Map<ColumnRefOperator, ScalarOperator> newScanProjectMap = scanOp.getProjection().getColumnRefMap().entrySet().stream()
                .collect(Collectors.toMap(
                        Map.Entry::getKey,
                        entry -> rewriteScalarOperatorByDistanceColumn(entry.getValue(), info, distanceColRef)
                ));

        LogicalOlapScanOperator newScanOp = LogicalOlapScanOperator.builder()
                .withOperator(scanOp)
                .setProjection(new Projection(newScanProjectMap))
                .setPredicate(newPredicate)
                .setColRefToColumnMetaMap(newColRefToColumnMetaMap)
                .setColumnMetaToColRefMap(newColumnMetaToColRefMap)
                .build();

        return OptExpression.create(topNOp, OptExpression.create(newScanOp));
    }

    ScalarOperator rewriteScalarOperatorByDistanceColumn(ScalarOperator scalarOperator, VectorFuncInfo info,
                                                         ColumnRefOperator distanceColRef) {
        if (scalarOperator.equals(info.vectorFuncCallOperator)) {
            return distanceColRef;
        }

        for (int i = 0; i < scalarOperator.getChildren().size(); i++) {
            ScalarOperator child = scalarOperator.getChild(i);
            scalarOperator.setChild(i, rewriteScalarOperatorByDistanceColumn(child, info, distanceColRef));
        }

        return scalarOperator;
    }

    /**
     * Check if the operator matches the specific vector function call.
     *
     * <p> For example, assume that `vectorFuncCallOperator` is `approx_l2_distance(v1, [1,2,3])`,
     * then the following operators match:
     * - `approx_l2_distance(v1, [1,2,3])`
     * - `cast(approx_l2_distance(v1, [1,2,3]) as float)`
     * - `cast(approx_l2_distance(v1, [1,2,3]) as double)`
     */
    private boolean matchesVectorFuncCall(CallOperator vectorFuncCallOperator, ScalarOperator operator) {
        if (operator instanceof CastOperator) {
            CastOperator castOperator = (CastOperator) operator;
            return castOperator.getType().isFloatingPointType() &&
                    matchesVectorFuncCall(vectorFuncCallOperator, castOperator.getChild(0));
        }

        if (operator instanceof CallOperator) {
            return vectorFuncCallOperator.equals(operator);
        }

        return false;
    }

    /**
     * Extract the vector range from the predicates.
     *
     * <p> Only the predicates in the following format can be parsed to vector range:
     * - req1: <=, >=, and one side is constant, the other side is the vector index column.
     * - req2: AND, and each child predicate meets req1.
     *
     * <p> For example, suppose v1 is the vector index column and isAscending=true, then:
     * - v1 <= 10: 10
     * - v1 <= 10 AND v1 < 20: 10
     * - v1 >= 10: cannot be parsed
     * - c1 <= 10: cannot be parsed
     * - v1 <= 10 and c1 < 10: cannot be parsed
     *
     * @return the vector range value if the predicate can be parsed to vector range, otherwise empty.
     */
    private Optional<Double> extractVectorRange(ScalarOperator predicate, VectorFuncInfo info) {
        if (predicate instanceof BinaryPredicateOperator) {
            return parseVectorRangeFromBinaryPredicate(predicate, info);
        } else if (predicate instanceof CompoundPredicateOperator) {
            CompoundPredicateOperator compoundPredicate = (CompoundPredicateOperator) predicate;
            if (!compoundPredicate.isAnd()) {
                return Optional.empty();
            }
            Optional<Double> value = Optional.empty();
            for (ScalarOperator child : predicate.getChildren()) {
                Optional<Double> childValue = parseVectorRangeFromBinaryPredicate(child, info);
                if (childValue.isEmpty()) {
                    return Optional.empty();
                }
                if (value.isEmpty()) {
                    value = childValue;
                } else {
                    if (info.isAscending) {
                        value = Optional.of(Math.min(value.get(), childValue.get()));
                    } else {
                        value = Optional.of(Math.max(value.get(), childValue.get()));
                    }
                }
            }
            return value;
        }

        return Optional.empty();
    }

    private Optional<Double> parseVectorRangeFromBinaryPredicate(ScalarOperator predicate, VectorFuncInfo info) {
        if (predicate instanceof BinaryPredicateOperator) {
            BinaryType binaryType = ((BinaryPredicateOperator) predicate).getBinaryType();
            ScalarOperator lhs = predicate.getChild(0);
            ScalarOperator rhs = predicate.getChild(1);

            if (rhs instanceof ConstantOperator && matchesVectorFuncCall(info.vectorFuncCallOperator, lhs) &&
                    (((binaryType.equals(LE)) && info.isAscending) || ((binaryType.equals(GE)) && !info.isAscending))) {
                return Optional.of((double) ((ConstantOperator) rhs).getValue());
            } else if (lhs instanceof ConstantOperator && matchesVectorFuncCall(info.vectorFuncCallOperator, rhs) &&
                    (((binaryType.equals(GE)) && info.isAscending) || ((binaryType.equals(LE)) && !info.isAscending))) {
                return Optional.of((double) ((ConstantOperator) lhs).getValue());
            }
        }

        return Optional.empty();
    }

    private boolean containsSupportedSpatialPredicate(ScalarOperator predicate) {
        if (predicate instanceof CallOperator) {
            String fnName = ((CallOperator) predicate).getFnName();
            if (fnName.equalsIgnoreCase(ST_CONTAINS) || fnName.equalsIgnoreCase(ST_DISTANCE_SPHERE)) {
                return true;
            }
        }

        for (ScalarOperator child : predicate.getChildren()) {
            if (containsSupportedSpatialPredicate(child)) {
                return true;
            }
        }

        return false;
    }

    /**
     * Read Grid-HNSW improvement flags from session variables first (query-time override),
     * falling back to index DDL properties if not set.
     * Session variables take priority so that all grid variants can share one table.
     */
    private void applyGridImprovementParams(OptimizerContext context,
                                            Map<String, String> props,
                                            VectorSearchOptions opts) {
        SessionVariable sv = context.getSessionVariable();

        if (sv.getVectorGridOversample() > 1.0) {
            opts.setGridOversample((float) sv.getVectorGridOversample());
        } else {
            String oversample = props.getOrDefault("grid_oversample",
                    props.getOrDefault("GRID_OVERSAMPLE", null));
            if (oversample != null) {
                try {
                    opts.setGridOversample(Float.parseFloat(oversample));
                } catch (NumberFormatException ignored) {
                }
            }
        }

        if (sv.isVectorGridExpandNeighbors()) {
            opts.setGridExpandNeighbors(true);
        } else {
            String expandNeighbors = props.getOrDefault("grid_expand_neighbors",
                    props.getOrDefault("GRID_EXPAND_NEIGHBORS", null));
            if ("true".equalsIgnoreCase(expandNeighbors)) {
                opts.setGridExpandNeighbors(true);
            }
        }

        if (sv.isVectorGridScanSmallCells()) {
            opts.setGridScanSmallCells(true);
        } else {
            String scanSmallCells = props.getOrDefault("grid_scan_small_cells",
                    props.getOrDefault("GRID_SCAN_SMALL_CELLS", null));
            if ("true".equalsIgnoreCase(scanSmallCells)) {
                opts.setGridScanSmallCells(true);
            }
        }

        if (sv.getVectorGridMaxCoverCells() != 500) {
            opts.setGridMaxCoverCells(sv.getVectorGridMaxCoverCells());
        } else {
            String maxCoverCells = props.getOrDefault("grid_max_cover_cells",
                    props.getOrDefault("GRID_MAX_COVER_CELLS", null));
            if (maxCoverCells != null) {
                try {
                    opts.setGridMaxCoverCells(Integer.parseInt(maxCoverCells));
                } catch (NumberFormatException ignored) {
                }
            }
        }
    }

    /**
     * Supported patterns:
     *   st_distance_sphere(lng_col, lat_col, const_lng, const_lat) <= const_radius
     *   st_distance_sphere(st_point(lng_col, lat_col), st_point(const_lng, const_lat)) <= const_radius
     *   st_contains(st_geomfromtext('POLYGON(...)'), st_point(lng_col, lat_col))
     */
    private void extractAcornSpatialParams(ScalarOperator predicate, LogicalOlapScanOperator scanOp,
                                           VectorSearchOptions opts) {
        if (predicate instanceof BinaryPredicateOperator) {
            BinaryPredicateOperator binOp = (BinaryPredicateOperator) predicate;
            BinaryType type = binOp.getBinaryType();
            ScalarOperator lhs = predicate.getChild(0);
            ScalarOperator rhs = predicate.getChild(1);
            
            if (lhs instanceof CallOperator && ((CallOperator) lhs).getFnName().equalsIgnoreCase(ST_DISTANCE_SPHERE)) {
                if (type == BinaryType.LT || type == BinaryType.LE) {
                    extractRadiusPredicate((CallOperator) lhs, rhs, scanOp, opts);
                    return;
                }
            }
            if (rhs instanceof CallOperator && ((CallOperator) rhs).getFnName().equalsIgnoreCase(ST_DISTANCE_SPHERE)) {
                if (type == BinaryType.GT || type == BinaryType.GE) {
                    extractRadiusPredicate((CallOperator) rhs, lhs, scanOp, opts);
                    return;
                }
            }
        }

        if (predicate instanceof CallOperator) {
            CallOperator call = (CallOperator) predicate;
            if (call.getFnName().equalsIgnoreCase(ST_CONTAINS)) {
                extractPolygonPredicate(call, scanOp, opts);
                return;
            }
        }

        if (predicate instanceof CompoundPredicateOperator) {
            for (ScalarOperator child : predicate.getChildren()) {
                extractAcornSpatialParams(child, scanOp, opts);
                if (opts.getAcornPredicateType() != VectorSearchOptions.AcornPredicateType.NONE) {
                    return;
                }
            }
        }
    }

    private void extractRadiusPredicate(CallOperator distCall, ScalarOperator boundOp,
                                        LogicalOlapScanOperator scanOp, VectorSearchOptions opts) {
        if (!(boundOp instanceof ConstantOperator)) {
            return;
        }
        double radius = ((Number) ((ConstantOperator) boundOp).getValue()).doubleValue();

        List<ScalarOperator> args = distCall.getChildren();
        String latCol = "";
        String lngCol = "";
        double centerLat = 0;
        double centerLng = 0;

        if (args.size() == 4) {
            // st_distance_sphere(lng_col, lat_col, const_lng, const_lat)
            if (args.get(0).isColumnRef()) {
                lngCol = resolveColumnName(args.get(0), scanOp);
            }
            if (args.get(1).isColumnRef()) {
                latCol = resolveColumnName(args.get(1), scanOp);
            }
            if (args.get(2) instanceof ConstantOperator) {
                centerLng = ((Number) ((ConstantOperator) args.get(2)).getValue()).doubleValue();
            }
            if (args.get(3) instanceof ConstantOperator) {
                centerLat = ((Number) ((ConstantOperator) args.get(3)).getValue()).doubleValue();
            }
        } else if (args.size() == 2) {
            // st_distance_sphere(st_point(lng_col, lat_col), st_point(const_lng, const_lat))
            double[] colLatLng = extractPointArgs(args.get(0), scanOp, true);
            double[] constLatLng = extractPointArgs(args.get(1), scanOp, false);
            if (colLatLng != null && constLatLng != null) {
                lngCol = resolveColumnName(((CallOperator) args.get(0)).getChild(0), scanOp);
                latCol = resolveColumnName(((CallOperator) args.get(0)).getChild(1), scanOp);
                centerLng = constLatLng[0];
                centerLat = constLatLng[1];
            }
        }

        if (!latCol.isEmpty() && !lngCol.isEmpty()) {
            opts.setAcornRadiusPredicate(centerLat, centerLng, radius, latCol, lngCol);
        }
    }

    private double[] extractPointArgs(ScalarOperator op, LogicalOlapScanOperator scanOp, boolean expectColumnRef) {
        if (!(op instanceof CallOperator)) {
            return null;
        }
        CallOperator call = (CallOperator) op;
        if (!call.getFnName().equalsIgnoreCase("st_point") || call.getChildren().size() != 2) {
            return null;
        }
        if (expectColumnRef) {
            if (call.getChild(0).isColumnRef() && call.getChild(1).isColumnRef()) {
                return new double[] {0, 0};
            }
        } else {
            if (call.getChild(0) instanceof ConstantOperator && call.getChild(1) instanceof ConstantOperator) {
                double lng = ((Number) ((ConstantOperator) call.getChild(0)).getValue()).doubleValue();
                double lat = ((Number) ((ConstantOperator) call.getChild(1)).getValue()).doubleValue();
                return new double[] {lng, lat};
            }
        }
        return null;
    }

    private void extractPolygonPredicate(CallOperator stContains, LogicalOlapScanOperator scanOp,
                                         VectorSearchOptions opts) {
        if (stContains.getChildren().size() != 2) {
            return;
        }

        ScalarOperator geomArg = stContains.getChild(0);
        ScalarOperator pointArg = stContains.getChild(1);

        String wkt = extractWktFromGeomFromText(geomArg);
        if (wkt == null || wkt.isEmpty()) {
            return;
        }

        String latCol = "";
        String lngCol = "";
        if (pointArg instanceof CallOperator) {
            CallOperator pointCall = (CallOperator) pointArg;
            if (pointCall.getFnName().equalsIgnoreCase("st_point") && pointCall.getChildren().size() == 2) {
                if (pointCall.getChild(0).isColumnRef()) {
                    lngCol = resolveColumnName(pointCall.getChild(0), scanOp);
                }
                if (pointCall.getChild(1).isColumnRef()) {
                    latCol = resolveColumnName(pointCall.getChild(1), scanOp);
                }
            }
        }

        if (!latCol.isEmpty() && !lngCol.isEmpty()) {
            opts.setAcornPolygonPredicate(wkt, latCol, lngCol);
        }
    }

    private String extractWktFromGeomFromText(ScalarOperator op) {
        if (op instanceof CallOperator) {
            CallOperator call = (CallOperator) op;
            if (call.getFnName().equalsIgnoreCase("st_geomfromtext") && call.getChildren().size() == 1) {
                if (call.getChild(0) instanceof ConstantOperator) {
                    return String.valueOf(((ConstantOperator) call.getChild(0)).getValue());
                }
            }
        }
        return null;
    }

    private String resolveColumnName(ScalarOperator op, LogicalOlapScanOperator scanOp) {
        if (op instanceof ColumnRefOperator) {
            Column col = scanOp.getColRefToColumnMetaMap().get(op);
            if (col != null) {
                return col.getName();
            }
        }
        return "";
    }

    /**
     * Extract the vector function information. If the vector index can be used, the following requirements need to be met:
     * 1. The first column of the ordering is the <approx_distance> function.
     * 2. The <approx_distance> function needs to match the metric_type and isAscending of the vector index.
     * - If the metric_type is L2_DISTANCE, then the <approx_distance> function is approx_l2_distance, and the order is ASC.
     * - If the metric_type is COSINE_SIMILARITY, then the <approx_distance> function is cosine_similarity, and the order is DESC.
     * 3. The arguments of the <approx_distance> function are the vector index column and a constant array.
     *
     * @return the vector function information if the ordering column is matched, otherwise empty.
     */
    private Optional<VectorFuncInfo> extractOrderByVectorFuncInfo(LogicalTopNOperator topNOp, LogicalOlapScanOperator scanOp) {
        OlapTable table = (OlapTable) scanOp.getTable();
        Index index = table.getIndexes().stream()
                .filter(i -> i.getIndexType() == IndexDef.IndexType.VECTOR)
                .findFirst()
                .orElse(null);
        if (index == null) {
            return Optional.empty();
        }

        ColumnRefOperator outColRef = topNOp.getOrderByElements().get(0).getColumnRef();
        final boolean isAscending = topNOp.getOrderByElements().get(0).isAscending();

        String rawMetricType = index.getProperties().get(VectorIndexParams.CommonIndexParamKey.METRIC_TYPE.name().toLowerCase());
        VectorIndexParams.MetricsType metricType =
                Enums.getIfPresent(VectorIndexParams.MetricsType.class, StringUtils.upperCase(rawMetricType)).orNull();
        Preconditions.checkNotNull(metricType, "Invalid metric type [" + rawMetricType + "] for vector index");

        // 1. Check: it is a matched vector function.
        ScalarOperator inOperator = scanOp.getProjection().getColumnRefMap().get(outColRef);
        if (!(inOperator instanceof CallOperator)) {
            return Optional.empty();
        }
        CallOperator inCallOperator = (CallOperator) inOperator;

        boolean matchedFunc;
        switch (metricType) {
            case L2_DISTANCE:
                matchedFunc = inCallOperator.getFnName().equalsIgnoreCase(APPROX_L2_DISTANCE) && isAscending;
                break;
            case COSINE_SIMILARITY:
                matchedFunc = inCallOperator.getFnName().equalsIgnoreCase(APPROX_COSINE_SIMILARITY) && !isAscending;
                break;
            default:
                matchedFunc = false;
        }
        if (!matchedFunc) {
            return Optional.empty();
        }

        // 2. Check: the vector function's arguments are column ref and constant.
        ScalarOperator lhs = inCallOperator.getChild(0);
        ScalarOperator rhs = inCallOperator.getChild(1);
        ColumnRefOperator colRefArgument;
        if (isConstantArrayFloat(lhs) && rhs.isColumnRef()) {
            colRefArgument = (ColumnRefOperator) rhs;
        } else if (isConstantArrayFloat(rhs) && lhs.isColumnRef()) {
            colRefArgument = (ColumnRefOperator) lhs;
        } else {
            return Optional.empty();
        }

        // 3. Check: the column ref argument of the vector function matches the index column.
        Column column = scanOp.getColRefToColumnMetaMap().get(colRefArgument);
        if (column == null) {
            return Optional.empty();
        }

        ColumnId indexColumnId = index.getColumns().get(0);
        if (!column.getColumnId().equals(indexColumnId)) {
            return Optional.empty();
        }

        // 4. Parse query vector values.
        List<String> vectorQuery = new ArrayList<>();
        extractValuesFromConstantArray(inCallOperator, vectorQuery);

        return Optional.of(
                new VectorFuncInfo(index, colRefArgument, outColRef, inCallOperator, metricType, vectorQuery, isAscending));
    }

    /**
     * Whether the scalar operator is a constant array of float, which is represented as
     * `ArrayOperator(type=ArrayType(float))` or
     * `CastOperator(child=ArrayOperator(type=ArrayType(numeric_type)), type=ArrayType(float))`.
     */
    private boolean isConstantArrayFloat(ScalarOperator scalarOperator) {
        if (!scalarOperator.isConstant()) {
            return false;
        }

        if (scalarOperator instanceof CastOperator) {
            if (!scalarOperator.getType().isArrayType()) {
                return false;
            }
            ArrayType arrayType = (ArrayType) scalarOperator.getType();
            if (!arrayType.getItemType().isFloatingPointType()) {
                return false;
            }

            return scalarOperator.getChildren().stream().allMatch(this::isConstantArrayFloat);
        } else if (scalarOperator instanceof ArrayOperator) {
            if (!scalarOperator.getType().isArrayType()) {
                return false;
            }
            ArrayType innerArrayType = (ArrayType) scalarOperator.getType();
            return innerArrayType.getItemType().isNumericType();
        } else {
            return false;
        }
    }

    private void extractValuesFromConstantArray(ScalarOperator scalarOperator, List<String> vectorQuery) {
        if (scalarOperator instanceof ColumnRefOperator) {
            return;
        }

        if (scalarOperator instanceof ConstantOperator) {
            vectorQuery.add(String.valueOf(((ConstantOperator) scalarOperator).getValue()));
            return;
        }

        for (ScalarOperator child : scalarOperator.getChildren()) {
            extractValuesFromConstantArray(child, vectorQuery);
        }
    }

    private static class VectorFuncInfo {
        private final Index index;
        // vector index column
        private final ColumnRefOperator inColumnRef;
        // The column ref of the first ordering column, which is obtained by vectorFuncCallOperator `<approx_distance>(inColumnRef, vectorQuery)`.
        // - If metricType is L2_DISTANCE, then <approx_distance> function is `approx_l2_distance`, and the order is ASC.
        // - If metricType is COSINE_SIMILARITY, then <approx_distance> function `is cosine_similarity`, and the order is DESC.
        private final ColumnRefOperator outColumnRef;
        private final CallOperator vectorFuncCallOperator;
        private final VectorIndexParams.MetricsType metricType;
        // The constant vector argument value of the <approx_distance> function
        private final List<String> vectorQuery;
        private final boolean isAscending;

        public VectorFuncInfo(Index index, ColumnRefOperator inColumnRef, ColumnRefOperator outColumnRef,
                              CallOperator vectorFuncCallOperator,
                              VectorIndexParams.MetricsType metricType, List<String> vectorQuery, boolean isAscending) {
            this.index = index;
            this.inColumnRef = inColumnRef;
            this.outColumnRef = outColumnRef;
            this.vectorFuncCallOperator = vectorFuncCallOperator;
            this.metricType = metricType;
            this.vectorQuery = vectorQuery;
            this.isAscending = isAscending;
        }
    }
}
