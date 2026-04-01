#!/usr/bin/env bash
#
# Grid-HNSW ONLY benchmark — fast iteration for grid ablation studies.
# Skips all ACORN/gamma variants. Uses cached B0 ground truth.
#
# Usage:
#   ./run_grid_benchmark.sh                           # first run (loads data)
#   ./run_grid_benchmark.sh --skip-load               # reuse existing grid table
#   ./run_grid_benchmark.sh --skip-load --skip-rebuild # fastest (no rebuild)
#
set +e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BENCH="$SCRIPT_DIR/run_benchmark.py"
COMPARE="$SCRIPT_DIR/compare_results.py"
REPORT="$SCRIPT_DIR/generate_report.py"
OUT="$SCRIPT_DIR/results/grid"

HOST="127.0.0.1"
PORT=9030
HTTP_PORT=8030
ROWS=100000
DIM=128
K=10
QUERIES=3
WARMUP=0
SKIP_LOAD=""
SKIP_REBUILD=0
GRID_OVERSAMPLE="10"
GRID_MAX_CELLS="1000"
STREAM_LOAD=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --host)           HOST="$2";           shift 2;;
        --port)           PORT="$2";           shift 2;;
        --http-port)      HTTP_PORT="$2";      shift 2;;
        --rows)           ROWS="$2";           shift 2;;
        --dim)            DIM="$2";            shift 2;;
        --k)              K="$2";              shift 2;;
        --queries)        QUERIES="$2";        shift 2;;
        --warmup)         WARMUP="$2";         shift 2;;
        --skip-load)      SKIP_LOAD="--skip-load"; shift;;
        --skip-rebuild)   SKIP_REBUILD=1;      shift;;
        --grid-oversample) GRID_OVERSAMPLE="$2"; shift 2;;
        --grid-max-cells) GRID_MAX_CELLS="$2"; shift 2;;
        --stream-load)    STREAM_LOAD="--stream-load --http-port $HTTP_PORT"; shift;;
        *) echo "Unknown arg: $1"; exit 1;;
    esac
done

COMMON="--host $HOST --port $PORT --rows $ROWS --dim $DIM --k $K --queries $QUERIES --warmup $WARMUP --output $OUT $STREAM_LOAD"

echo "============================================================"
echo " Grid-HNSW Focused Benchmark (fast mode)"
echo "============================================================"
echo "  Host:       $HOST:$PORT"
echo "  Rows:       $ROWS  |  Dim: $DIM  |  K: $K"
echo "  Queries:    $QUERIES (+ $WARMUP warmup)"
echo "  Oversample: $GRID_OVERSAMPLE"
echo "  Max cells:  $GRID_MAX_CELLS"
echo ""

mkdir -p "$OUT"

# ------------------------------------------------------------------
# Step 1: Optional FE rebuild + restart
# ------------------------------------------------------------------
if [ "$SKIP_REBUILD" -eq 0 ]; then
    echo ">> Rebuilding FE..."
    if [ -f "$(dirname "$SCRIPT_DIR")/../../../docker-compose.dev.yml" ]; then
        PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
        (cd "$PROJECT_ROOT" && docker-compose -f docker-compose.dev.yml run --rm build-fe 2>&1 | tail -5)
        FE_BUILD_RC=$?
        if [ "$FE_BUILD_RC" -ne 0 ]; then
            echo "ERROR: FE build failed (exit code $FE_BUILD_RC)"
            exit 1
        fi
        echo ">> Restarting FE..."
        docker-compose -f "$PROJECT_ROOT/docker-compose.dev.yml" restart starrocks-custom-fe
        echo "   Waiting 25s for FE to start..."
        sleep 25
    else
        echo "   No docker-compose.dev.yml found — skipping rebuild."
    fi
fi

# ------------------------------------------------------------------
# Step 2: Verify StarRocks
# ------------------------------------------------------------------
echo ">> Verifying StarRocks at $HOST:$PORT ..."
for i in 1 2 3 4 5; do
    if mysql -h "$HOST" -P "$PORT" -u root -e "SELECT 1" >/dev/null 2>&1; then
        echo "   OK"
        break
    fi
    if [ "$i" -eq 5 ]; then
        echo "ERROR: Cannot connect to StarRocks at $HOST:$PORT after 5 attempts"
        exit 1
    fi
    echo "   Attempt $i failed, waiting 10s..."
    sleep 10
done
echo ""

# Enable vector index feature
mysql -h "$HOST" -P "$PORT" -u root -e 'ADMIN SET FRONTEND CONFIG ("enable_experimental_vector" = "true")' 2>/dev/null

# ------------------------------------------------------------------
# Pre-flight: verify FE has grid session variables
# ------------------------------------------------------------------
echo ">> Pre-flight: checking FE session variables..."
SET_RESULT=$(mysql -h "$HOST" -P "$PORT" -u root -N -e "SET vector_grid_oversample = 5.0; SHOW VARIABLES LIKE 'vector_grid_oversample'" 2>&1)
if echo "$SET_RESULT" | grep -qi "error\|unknown\|variable.*not"; then
    echo ""
    echo "FATAL: FE does not have grid session variables."
    echo "       The running FE binary is outdated. You MUST rebuild:"
    echo ""
    echo "  docker-compose -f docker-compose.dev.yml run --rm build-fe"
    echo "  docker-compose -f docker-compose.dev.yml restart starrocks-custom-fe"
    echo "  sleep 30"
    echo ""
    echo "  Then re-run this script."
    exit 1
fi
echo "   OK — grid session variables recognized"
echo ""

# ------------------------------------------------------------------
# Step 3: Ensure B0 ground truth exists in grid results dir
# ------------------------------------------------------------------
PARENT_OUT="$(dirname "$OUT")"
B0_RESULTS=$(ls -t "$OUT"/b0_*.json 2>/dev/null | head -1)
if [ -z "$B0_RESULTS" ]; then
    PARENT_B0=$(ls -t "$PARENT_OUT"/b0_*.json 2>/dev/null | head -1)
    if [ -n "$PARENT_B0" ]; then
        echo ">> Copying B0 ground truth from parent results dir..."
        cp "$PARENT_B0" "$OUT/"
        B0_RESULTS="$OUT/$(basename "$PARENT_B0")"
    else
        echo ">> No B0 ground truth found. Running baseline (one-time)..."
        python3 "$BENCH" --mode b0 $COMMON
    fi
    echo ""
else
    echo ">> Using cached B0 ground truth: $(basename "$B0_RESULTS")"
fi

# ------------------------------------------------------------------
# Step 4: Clean stale grid results
# ------------------------------------------------------------------
echo ">> Cleaning stale grid result files..."
rm -f "$OUT"/grid_*.json "$OUT"/grid.json 2>/dev/null
rm -f "$OUT"/grid_g*.json 2>/dev/null
echo ""

# ------------------------------------------------------------------
# Step 5: Grid baseline (load data if needed)
# ------------------------------------------------------------------
echo "============================================================"
echo " [1/8] Grid-HNSW Baseline"
echo "============================================================"
python3 "$BENCH" --mode grid $COMMON $SKIP_LOAD
echo ""

# ------------------------------------------------------------------
# Step 6: Grid ablation (all reuse same table, --skip-load always)
# ------------------------------------------------------------------
GRID_COMMON="$COMMON --skip-load"

echo "============================================================"
echo " [2/8] G1: Oversample (factor=$GRID_OVERSAMPLE)"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" $GRID_COMMON
echo ""

echo "============================================================"
echo " [3/8] G2: Neighbor Expansion"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-expand-neighbors $GRID_COMMON
echo ""

echo "============================================================"
echo " [4/8] G3: Small-Cell Scan"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-scan-small $GRID_COMMON
echo ""

echo "============================================================"
echo " [5/8] G4: Max Cover Cells ($GRID_MAX_CELLS)"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-max-cells "$GRID_MAX_CELLS" $GRID_COMMON
echo ""

echo "============================================================"
echo " [6/8] G1+G2: Oversample + Neighbors"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors $GRID_COMMON
echo ""

echo "============================================================"
echo " [7/8] G1+G2+G3: Oversample + Neighbors + SmallCells"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors --grid-scan-small $GRID_COMMON
echo ""

echo "============================================================"
echo " [8/8] G1+G2+G3+G4: All Improvements"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors --grid-scan-small --grid-max-cells "$GRID_MAX_CELLS" $GRID_COMMON
echo ""

# ------------------------------------------------------------------
# Step 7: Report
# ------------------------------------------------------------------
echo "============================================================"
echo " Comparison"
echo "============================================================"
python3 "$COMPARE" --results-dir "$OUT" 2>/dev/null
echo ""

if python3 -c "import matplotlib" 2>/dev/null; then
    python3 "$REPORT" --results-dir "$OUT" --output "$OUT/benchmark_report.html"
    echo "Report: $OUT/benchmark_report.html"
fi

echo ""
echo "Done."
