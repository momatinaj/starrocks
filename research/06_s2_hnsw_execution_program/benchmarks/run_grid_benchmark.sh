#!/usr/bin/env bash
#
# Grid-HNSW focused benchmark: rebuild FE, restart, and run ONLY grid
# variants (baseline + ablation). Reuses existing data and cached results
# for other modes (B0, ACORN, ACORN-gamma) in the report.
#
# Usage:
#   # First run (load data into grid table)
#   ./run_grid_benchmark.sh
#
#   # Subsequent runs after code changes (skip data loading)
#   ./run_grid_benchmark.sh --skip-load
#
#   # Skip FE rebuild (just re-run queries)
#   ./run_grid_benchmark.sh --skip-load --skip-rebuild
#
#   # Custom oversample / max-cells
#   ./run_grid_benchmark.sh --skip-load --grid-oversample 5 --grid-max-cells 64
#
#   # Use Stream Load for faster data loading
#   ./run_grid_benchmark.sh --stream-load
#

set +e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
BENCH="$SCRIPT_DIR/run_benchmark.py"
COMPARE="$SCRIPT_DIR/compare_results.py"
OUT="$SCRIPT_DIR/results"

HOST="127.0.0.1"
PORT=9030
HTTP_PORT=8030
ROWS=100000
DIM=128
K=10
QUERIES=50
WARMUP=5
SKIP_LOAD=""
SKIP_REBUILD=0
GRID_OVERSAMPLE="3"
GRID_MAX_CELLS="500"
STREAM_LOAD=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --host)    HOST="$2";    shift 2;;
        --port)    PORT="$2";    shift 2;;
        --http-port) HTTP_PORT="$2"; shift 2;;
        --rows)    ROWS="$2";    shift 2;;
        --dim)     DIM="$2";     shift 2;;
        --k)       K="$2";      shift 2;;
        --queries) QUERIES="$2"; shift 2;;
        --warmup)  WARMUP="$2";  shift 2;;
        --skip-load) SKIP_LOAD="--skip-load"; shift;;
        --skip-rebuild) SKIP_REBUILD=1; shift;;
        --grid-oversample) GRID_OVERSAMPLE="$2"; shift 2;;
        --grid-max-cells)  GRID_MAX_CELLS="$2"; shift 2;;
        --stream-load) STREAM_LOAD="--stream-load --http-port $HTTP_PORT"; shift;;
        *) echo "Unknown arg: $1"; exit 1;;
    esac
done

COMMON="--host $HOST --port $PORT --rows $ROWS --dim $DIM --k $K --queries $QUERIES --warmup $WARMUP --output $OUT $STREAM_LOAD"

echo "============================================================"
echo " Grid-HNSW Focused Benchmark"
echo "============================================================"
echo "  Host:       $HOST:$PORT"
echo "  Rows:       $ROWS"
echo "  Dim:        $DIM"
echo "  K:          $K"
echo "  Queries:    $QUERIES (+ $WARMUP warmup per spec)"
echo "  Oversample: $GRID_OVERSAMPLE"
echo "  Max cells:  $GRID_MAX_CELLS"
echo "  Output:     $OUT"
echo ""

mkdir -p "$OUT"

# ------------------------------------------------------------------
# Step 1: Optional FE rebuild + restart
# ------------------------------------------------------------------
if [ "$SKIP_REBUILD" -eq 0 ]; then
    echo ">> Rebuilding FE..."
    if [ -f "$PROJECT_ROOT/docker-compose.dev.yml" ]; then
        (cd "$PROJECT_ROOT" && docker-compose -f docker-compose.dev.yml run --rm build-fe 2>&1 | tail -5)
        echo ">> Restarting FE..."
        docker-compose -f "$PROJECT_ROOT/docker-compose.dev.yml" restart starrocks-custom-fe
        echo "   Waiting 20s for FE to start..."
        sleep 20
    else
        echo "   No docker-compose.dev.yml found — skipping rebuild."
        echo "   Use --skip-rebuild if FE is already running with latest code."
    fi
fi

# ------------------------------------------------------------------
# Step 2: Verify StarRocks
# ------------------------------------------------------------------
echo ">> Verifying StarRocks..."
if ! mysql -h "$HOST" -P "$PORT" -u root -e "SELECT 1" >/dev/null 2>&1; then
    echo "ERROR: Cannot connect to StarRocks at $HOST:$PORT"
    exit 1
fi
echo "   OK"
echo ""

# ------------------------------------------------------------------
# Step 3: Check for B0 ground truth
# ------------------------------------------------------------------
B0_RESULTS=$(ls -t "$OUT"/b0_*.json 2>/dev/null | head -1)
if [ -z "$B0_RESULTS" ]; then
    echo ">> No B0 ground truth found. Running baseline first..."
    python3 "$BENCH" --mode b0 $COMMON
    echo ""
else
    echo ">> Using cached B0 ground truth: $(basename $B0_RESULTS)"
fi

# ------------------------------------------------------------------
# Step 4: Grid baseline (load data if needed)
# ------------------------------------------------------------------
echo "============================================================"
echo " Grid-HNSW Baseline"
echo "============================================================"
python3 "$BENCH" --mode grid $COMMON $SKIP_LOAD
echo ""

# ------------------------------------------------------------------
# Step 5: Grid ablation variants (always --skip-load, reuse same table)
# ------------------------------------------------------------------
GRID_COMMON="$COMMON --skip-load"

echo "============================================================"
echo " Grid Ablation: G1 -- Oversample (factor=${GRID_OVERSAMPLE})"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" $GRID_COMMON
echo ""

echo "============================================================"
echo " Grid Ablation: G2 -- Neighbor Expansion"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-expand-neighbors $GRID_COMMON
echo ""

echo "============================================================"
echo " Grid Ablation: G3 -- Small-Cell Scan"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-scan-small $GRID_COMMON
echo ""

echo "============================================================"
echo " Grid Ablation: G4 -- Max Cover Cells (${GRID_MAX_CELLS})"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-max-cells "$GRID_MAX_CELLS" $GRID_COMMON
echo ""

echo "============================================================"
echo " Grid Ablation: G1+G2 -- Oversample + Neighbors"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors $GRID_COMMON
echo ""

echo "============================================================"
echo " Grid Ablation: G1+G2+G3 -- All except max_cells"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors --grid-scan-small $GRID_COMMON
echo ""

echo "============================================================"
echo " Grid Ablation: G1+G2+G3+G4 -- All Improvements"
echo "============================================================"
python3 "$BENCH" --mode grid --grid-oversample "$GRID_OVERSAMPLE" --grid-expand-neighbors --grid-scan-small --grid-max-cells "$GRID_MAX_CELLS" $GRID_COMMON
echo ""

# ------------------------------------------------------------------
# Step 6: Comparison and report
# ------------------------------------------------------------------
echo "============================================================"
echo " Comparison (all modes)"
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
    echo "  matplotlib not installed — skipping charts."
    echo "  Install with: pip3 install matplotlib"
    echo "  Then run:     python3 $REPORT --results-dir $OUT"
fi

echo ""
echo "Done. JSON results are in $OUT/"
