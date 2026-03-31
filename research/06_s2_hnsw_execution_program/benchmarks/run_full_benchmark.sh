#!/usr/bin/env bash
#
# Full benchmark: B0 (brute force) vs ACORN-1 vs ACORN-gamma vs Grid-HNSW
# Runs all modes sequentially on the custom StarRocks cluster,
# then prints a side-by-side comparison table.
#
# Usage:
#   ./run_full_benchmark.sh                        # default 100K rows, port 9030
#   ./run_full_benchmark.sh --rows 200000          # custom row count
#   ./run_full_benchmark.sh --skip-load            # reuse existing tables
#   ./run_full_benchmark.sh --skip-baseline        # skip B0 brute force (uses cached results)
#   ./run_full_benchmark.sh --skip-load --skip-baseline  # fastest re-run
#   ./run_full_benchmark.sh --only acorn --skip-load     # run only ACORN-1
#   ./run_full_benchmark.sh --only acorn_gamma --skip-load  # run only ACORN-gamma
#   ./run_full_benchmark.sh --only grid  --skip-load     # run only Grid-HNSW
#
# When do you need to reload data?
#   - First run ever:       data is generated and loaded automatically
#   - Changed --rows/--dim: must reload (remove --skip-load)
#   - Code changes only:    use --skip-load (tables already exist)
#   - Different server:     must reload (no --skip-load)
#

set +e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH="$SCRIPT_DIR/run_benchmark.py"
COMPARE="$SCRIPT_DIR/compare_results.py"
OUT="$SCRIPT_DIR/results"

HOST="127.0.0.1"
PORT=9030
ROWS=100000
DIM=128
K=10
QUERIES=50
WARMUP=5
SKIP_LOAD=""
SKIP_BASELINE=0
ONLY_MODE=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --host)    HOST="$2";    shift 2;;
        --port)    PORT="$2";    shift 2;;
        --rows)    ROWS="$2";    shift 2;;
        --dim)     DIM="$2";     shift 2;;
        --k)       K="$2";      shift 2;;
        --queries) QUERIES="$2"; shift 2;;
        --warmup)  WARMUP="$2";  shift 2;;
        --skip-load) SKIP_LOAD="--skip-load"; shift;;
        --skip-baseline) SKIP_BASELINE=1; shift;;
        --only)    ONLY_MODE="$2"; SKIP_BASELINE=1; shift 2;;
        *) echo "Unknown arg: $1"; exit 1;;
    esac
done

COMMON_ARGS="--host $HOST --port $PORT --rows $ROWS --dim $DIM --k $K --queries $QUERIES --warmup $WARMUP --output $OUT $SKIP_LOAD"

echo "============================================================"
echo " Full Benchmark: B0 vs ACORN-1 vs ACORN-gamma vs Grid-HNSW"
echo "============================================================"
echo "  Host:    $HOST:$PORT"
echo "  Rows:    $ROWS"
echo "  Dim:     $DIM"
echo "  K:       $K"
echo "  Queries: $QUERIES (+ $WARMUP warmup per spec)"
echo "  Output:  $OUT"
echo ""

mkdir -p "$OUT"

# Clean up stale B2/A0 result files so the report only shows active modes
rm -f "$OUT"/b2_*.json "$OUT"/a0_*.json 2>/dev/null

echo ">> Verifying StarRocks is reachable on port $PORT ..."
if ! mysql -h "$HOST" -P "$PORT" -u root -e "SELECT 1" >/dev/null 2>&1; then
    echo "ERROR: Cannot connect to StarRocks at $HOST:$PORT"
    echo "Start the cluster first:"
    echo "  docker-compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be"
    exit 1
fi
echo "   OK"

echo ">> Checking ACORN index type support..."
ACORN_CHECK=$(mysql -h "$HOST" -P "$PORT" -u root -N -e "
  CREATE DATABASE IF NOT EXISTS bench_spatial_vector;
  USE bench_spatial_vector;
  DROP TABLE IF EXISTS tmp_acorn_probe;
  CREATE TABLE tmp_acorn_probe (
    id BIGINT NOT NULL,
    v ARRAY<FLOAT> NOT NULL,
    INDEX vi (v) USING VECTOR(\"index_type\"=\"acorn\",\"dim\"=\"4\",\"metric_type\"=\"l2_distance\",\"is_vector_normed\"=\"false\",\"M\"=\"16\",\"efconstruction\"=\"40\")
  ) ENGINE=OLAP DUPLICATE KEY(id) DISTRIBUTED BY HASH(id) BUCKETS 1 PROPERTIES(\"replication_num\"=\"1\");
  DROP TABLE IF EXISTS tmp_acorn_probe;
" 2>&1) || true
if echo "$ACORN_CHECK" | grep -qi "must in\|should not"; then
    echo ""
    echo "ERROR: The running FE does not support index_type=ACORN."
    echo "       Rebuild the FE with the latest code."
    echo "       Error: $ACORN_CHECK"
    echo ""
    exit 1
fi
echo "   OK"

echo ">> Checking ACORN_GAMMA index type support..."
ACORN_GAMMA_CHECK=$(mysql -h "$HOST" -P "$PORT" -u root -N -e "
  CREATE DATABASE IF NOT EXISTS bench_spatial_vector;
  USE bench_spatial_vector;
  DROP TABLE IF EXISTS tmp_acorn_gamma_probe;
  CREATE TABLE tmp_acorn_gamma_probe (
    id BIGINT NOT NULL,
    v ARRAY<FLOAT> NOT NULL,
    INDEX vi (v) USING VECTOR(\"index_type\"=\"acorn_gamma\",\"dim\"=\"4\",\"metric_type\"=\"l2_distance\",\"is_vector_normed\"=\"false\",\"M\"=\"16\",\"efconstruction\"=\"40\",\"gamma\"=\"2\")
  ) ENGINE=OLAP DUPLICATE KEY(id) DISTRIBUTED BY HASH(id) BUCKETS 1 PROPERTIES(\"replication_num\"=\"1\");
  DROP TABLE IF EXISTS tmp_acorn_gamma_probe;
" 2>&1) || true
if echo "$ACORN_GAMMA_CHECK" | grep -qi "must in\|should not"; then
    echo ""
    echo "WARNING: The running FE does not support index_type=ACORN_GAMMA."
    echo "         ACORN-gamma benchmark will be skipped."
    echo ""
    SKIP_ACORN_GAMMA=1
else
    SKIP_ACORN_GAMMA=0
fi
echo "   OK"

echo ">> Checking GRID_HNSW index type support..."
GRID_CHECK=$(mysql -h "$HOST" -P "$PORT" -u root -N -e "
  CREATE DATABASE IF NOT EXISTS bench_spatial_vector;
  USE bench_spatial_vector;
  DROP TABLE IF EXISTS tmp_grid_probe;
  CREATE TABLE tmp_grid_probe (
    id BIGINT NOT NULL,
    lat DOUBLE NOT NULL,
    lng DOUBLE NOT NULL,
    v ARRAY<FLOAT> NOT NULL,
    INDEX vi (v) USING VECTOR(\"index_type\"=\"grid_hnsw\",\"dim\"=\"4\",\"metric_type\"=\"l2_distance\",\"is_vector_normed\"=\"false\",\"M\"=\"16\",\"efconstruction\"=\"40\",\"s2_level\"=\"12\",\"lat_column\"=\"lat\",\"lng_column\"=\"lng\")
  ) ENGINE=OLAP DUPLICATE KEY(id) DISTRIBUTED BY HASH(id) BUCKETS 1 PROPERTIES(\"replication_num\"=\"1\");
  DROP TABLE IF EXISTS tmp_grid_probe;
" 2>&1) || true
if echo "$GRID_CHECK" | grep -qi "must in\|should not"; then
    echo ""
    echo "ERROR: The running FE does not support index_type=GRID_HNSW."
    echo "       Rebuild the FE with the latest code."
    echo "       Error: $GRID_CHECK"
    echo ""
    exit 1
fi
echo "   OK"
echo ""

if [ "$SKIP_BASELINE" -eq 1 ]; then
    B0_RESULTS=$(ls -t "$OUT"/b0_*.json 2>/dev/null | head -1)
    if [ -z "$B0_RESULTS" ]; then
        echo "WARNING: --skip-baseline used but no B0 results found in $OUT/"
        echo "         Running baseline anyway for ground truth."
        SKIP_BASELINE=0
    else
        echo ">> Skipping B0 baseline (using cached: $(basename $B0_RESULTS))"
        echo ""
    fi
fi

if [ "$SKIP_BASELINE" -eq 0 ]; then
    echo "============================================================"
    echo " [1/4] B0 -- Brute Force (Ground Truth)"
    echo "============================================================"
    python3 "$BENCH" --mode b0 $COMMON_ARGS
    echo ""
else
    echo "============================================================"
    echo " [1/4] B0 -- SKIPPED (cached results)"
    echo "============================================================"
    echo ""
fi

# ACORN and Grid-HNSW clone data from B0 table (fast bulk copy) instead of
# row-by-row INSERT. --clone-from is ignored when --skip-load is active and
# the table already exists.
CLONE_ARG=""
if [ -z "$SKIP_LOAD" ]; then
    CLONE_ARG="--clone-from b0"
fi

if [ -z "$ONLY_MODE" ] || [ "$ONLY_MODE" = "acorn" ]; then
    echo "============================================================"
    echo " [2/4] ACORN-1 -- Predicate-Aware Search"
    echo "============================================================"
    python3 "$BENCH" --mode acorn $COMMON_ARGS $CLONE_ARG
    echo ""
fi

if ([ -z "$ONLY_MODE" ] || [ "$ONLY_MODE" = "acorn_gamma" ]) && [ "${SKIP_ACORN_GAMMA:-0}" -eq 0 ]; then
    echo "============================================================"
    echo " [3/4] ACORN-gamma -- Dense-Graph Predicate-Aware Search"
    echo "============================================================"
    python3 "$BENCH" --mode acorn_gamma $COMMON_ARGS $CLONE_ARG
    echo ""
fi

if [ -z "$ONLY_MODE" ] || [ "$ONLY_MODE" = "grid" ]; then
    echo "============================================================"
    echo " [4/4] GRID -- Grid-HNSW Spatially Partitioned Search"
    echo "============================================================"
    python3 "$BENCH" --mode grid $COMMON_ARGS $CLONE_ARG
    echo ""
fi

echo "============================================================"
echo " Comparison"
echo "============================================================"
python3 "$COMPARE" --results-dir "$OUT"

echo ""
echo "============================================================"
echo " Generating Report"
echo "============================================================"
REPORT="$SCRIPT_DIR/generate_report.py"
if python3 -c "import matplotlib" 2>/dev/null; then
    python3 "$REPORT" --results-dir "$OUT" --output "$OUT/benchmark_report.html"
    echo ""
    echo "Open the report:  open $OUT/benchmark_report.html"
else
    echo "  matplotlib not installed -- skipping charts."
    echo "  Install with: pip3 install matplotlib"
    echo "  Then run:     python3 $REPORT --results-dir $OUT"
fi

echo ""
echo "Done. JSON results are in $OUT/"
