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

echo ""
echo "============================================================"
echo " Grid-HNSW Focused Benchmark"
echo "============================================================"
echo "  Host:       $HOST:$PORT"
echo "  Rows:       $ROWS  |  Dim: $DIM  |  K: $K"
echo "  Queries:    $QUERIES per spec (+ $WARMUP warmup)"
echo "  Oversample: baseline=5.0, G1=$GRID_OVERSAMPLE"
echo "  Max cells:  baseline=500, G4=$GRID_MAX_CELLS"
echo "  Output:     $OUT"
echo "============================================================"
echo ""

mkdir -p "$OUT"

# ------------------------------------------------------------------
# Step 1: Optional FE rebuild + restart
# ------------------------------------------------------------------
if [ "$SKIP_REBUILD" -eq 0 ]; then
    echo ">> Rebuilding FE..."
    PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
    if [ -f "$PROJECT_ROOT/docker-compose.dev.yml" ]; then
        (cd "$PROJECT_ROOT" && docker-compose -f docker-compose.dev.yml run --rm build-fe 2>&1 | tail -5)
        FE_BUILD_RC=$?
        if [ "$FE_BUILD_RC" -ne 0 ]; then
            echo "ERROR: FE build failed (exit code $FE_BUILD_RC)"
            exit 1
        fi
        echo ">> Restarting FE container..."
        (cd "$PROJECT_ROOT" && docker-compose -f docker-compose.dev.yml restart starrocks-custom-fe)
        echo "   Waiting 30s for FE to start..."
        sleep 30
    else
        echo "   No docker-compose.dev.yml found — skipping rebuild."
    fi
fi

# ------------------------------------------------------------------
# Step 2: Verify StarRocks connectivity
# ------------------------------------------------------------------
echo ">> Checking StarRocks connectivity..."
for i in $(seq 1 10); do
    if mysql -h "$HOST" -P "$PORT" -u root -e "SELECT 1" >/dev/null 2>&1; then
        echo "   Connected OK (attempt $i)"
        break
    fi
    if [ "$i" -eq 10 ]; then
        echo ""
        echo "FATAL: Cannot connect to StarRocks at $HOST:$PORT after 10 attempts."
        echo "       Try: docker-compose -f docker-compose.dev.yml restart"
        exit 1
    fi
    echo "   Attempt $i failed, retrying in 5s..."
    sleep 5
done
echo ""

mysql -h "$HOST" -P "$PORT" -u root -e 'ADMIN SET FRONTEND CONFIG ("enable_experimental_vector" = "true")' 2>/dev/null

# ------------------------------------------------------------------
# Step 3: Pre-flight — verify FE has grid session variables
# ------------------------------------------------------------------
echo ">> Pre-flight: verifying grid session variables in FE..."
SET_CHECK=$(mysql -h "$HOST" -P "$PORT" -u root -N -e \
    "SET vector_grid_oversample = 5.0; SELECT @@vector_grid_oversample" 2>&1)

if echo "$SET_CHECK" | grep -qiE "error|unknown|not exist"; then
    echo ""
    echo "========================================================"
    echo " FATAL: FE binary does not support grid session variables"
    echo "========================================================"
    echo ""
    echo " You MUST rebuild FE before running grid ablation:"
    echo "   cd $(cd "$SCRIPT_DIR/../../.." && pwd)"
    echo "   docker-compose -f docker-compose.dev.yml run --rm build-fe"
    echo "   docker-compose -f docker-compose.dev.yml restart starrocks-custom-fe"
    echo "   sleep 30"
    echo ""
    exit 1
fi
echo "   OK — session variables supported (oversample = $(echo "$SET_CHECK" | tail -1))"
echo ""

# ------------------------------------------------------------------
# Step 4: Ensure B0 ground truth exists
# ------------------------------------------------------------------
PARENT_OUT="$(dirname "$OUT")"
B0_RESULTS=$(ls -t "$OUT"/b0_*.json 2>/dev/null | head -1)
if [ -z "$B0_RESULTS" ]; then
    PARENT_B0=$(ls -t "$PARENT_OUT"/b0_*.json 2>/dev/null | head -1)
    if [ -n "$PARENT_B0" ]; then
        echo ">> Copying B0 ground truth from parent results dir..."
        cp "$PARENT_B0" "$OUT/"
        echo "   Copied: $(basename "$PARENT_B0")"
    else
        echo ">> No B0 ground truth found anywhere. Running B0 (one-time)..."
        python3 "$BENCH" --mode b0 $COMMON
    fi
else
    echo ">> B0 ground truth: $(basename "$B0_RESULTS")"
fi
echo ""

# ------------------------------------------------------------------
# Step 5: CLEAN ALL stale grid results (critical!)
# ------------------------------------------------------------------
echo ">> Cleaning ALL stale grid results in $OUT ..."
rm -f "$OUT"/grid*.json 2>/dev/null
echo "   Done"
echo ""

# ------------------------------------------------------------------
# Step 6: Grid-HNSW baseline (load data if needed)
# ------------------------------------------------------------------
echo "============================================================"
echo " [1/8] Grid-HNSW BASELINE (oversample=5.0, max_cells=500)"
echo "============================================================"
python3 "$BENCH" --mode grid $COMMON $SKIP_LOAD
RC=$?
if [ "$RC" -ne 0 ]; then
    echo "ERROR: Grid baseline failed (exit $RC). Fix issues and retry."
    exit 1
fi
echo ""

# From here on, all variants reuse the same table — always --skip-load
GRID_COMMON="$COMMON --skip-load"

# ------------------------------------------------------------------
# Step 7: Grid ablation variants (each differs from baseline by one param)
# ------------------------------------------------------------------
echo "============================================================"
echo " [2/8] G1: Oversample=$GRID_OVERSAMPLE (baseline=5.0)"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" $GRID_COMMON
echo ""

echo "============================================================"
echo " [3/8] G2: Expand Neighbor Cells"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-expand-neighbors $GRID_COMMON
echo ""

echo "============================================================"
echo " [4/8] G3: Brute-Force Small Cells"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-scan-small $GRID_COMMON
echo ""

echo "============================================================"
echo " [5/8] G4: Max Cover Cells=$GRID_MAX_CELLS (baseline=500)"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-max-cells "$GRID_MAX_CELLS" $GRID_COMMON
echo ""

echo "============================================================"
echo " [6/8] G1+G2: Oversample=$GRID_OVERSAMPLE + Neighbors"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors $GRID_COMMON
echo ""

echo "============================================================"
echo " [7/8] G1+G2+G3: Oversample + Neighbors + SmallCells"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors --grid-scan-small $GRID_COMMON
echo ""

echo "============================================================"
echo " [8/8] ALL: Oversample + Neighbors + SmallCells + MaxCells"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors --grid-scan-small --grid-max-cells "$GRID_MAX_CELLS" $GRID_COMMON
echo ""

# ------------------------------------------------------------------
# Step 8: Generate comparison + report
# ------------------------------------------------------------------
echo "============================================================"
echo " Results"
echo "============================================================"
echo ""
python3 "$COMPARE" --results-dir "$OUT" 2>/dev/null
echo ""

REPORT_FILE="$OUT/grid_benchmark_report.html"
if python3 -c "import matplotlib" 2>/dev/null; then
    python3 "$REPORT" --results-dir "$OUT" --output "$REPORT_FILE"
    echo ""
    echo "HTML Report: $REPORT_FILE"
else
    echo "(matplotlib not available — skipping HTML report)"
fi

echo ""
echo "============================================================"
echo " DONE — Grid-HNSW ablation complete"
echo " Results in: $OUT"
echo "============================================================"
