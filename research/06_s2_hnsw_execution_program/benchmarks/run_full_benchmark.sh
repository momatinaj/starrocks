#!/usr/bin/env bash
#
# Full benchmark: B0 (brute force) vs B2 (HNSW fallback) vs ACORN-1
# Runs all three modes sequentially on the custom StarRocks cluster,
# then prints a side-by-side comparison table.
#
# Usage:
#   ./run_full_benchmark.sh                    # default 100K rows, port 9030
#   ./run_full_benchmark.sh --rows 200000      # custom row count
#   ./run_full_benchmark.sh --skip-load        # reuse existing tables
#

set -euo pipefail

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
        *) echo "Unknown arg: $1"; exit 1;;
    esac
done

COMMON_ARGS="--host $HOST --port $PORT --rows $ROWS --dim $DIM --k $K --queries $QUERIES --warmup $WARMUP --output $OUT $SKIP_LOAD"

echo "============================================================"
echo " Full Benchmark: B0 vs B2 vs ACORN-1"
echo "============================================================"
echo "  Host:    $HOST:$PORT"
echo "  Rows:    $ROWS"
echo "  Dim:     $DIM"
echo "  K:       $K"
echo "  Queries: $QUERIES (+ $WARMUP warmup per spec)"
echo "  Output:  $OUT"
echo ""

mkdir -p "$OUT"

echo ">> Verifying StarRocks is reachable on port $PORT ..."
if ! mysql -h "$HOST" -P "$PORT" -u root -e "SELECT 1" >/dev/null 2>&1; then
    echo "ERROR: Cannot connect to StarRocks at $HOST:$PORT"
    echo "Start the cluster first:"
    echo "  docker compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be"
    exit 1
fi
echo "   OK"
echo ""

echo "============================================================"
echo " [1/3] B0 -- Brute Force (Ground Truth)"
echo "============================================================"
python3 "$BENCH" --mode b0 $COMMON_ARGS
echo ""

echo "============================================================"
echo " [2/3] B2 -- HNSW + Planner Fallback"
echo "============================================================"
python3 "$BENCH" --mode b2 $COMMON_ARGS
echo ""

echo "============================================================"
echo " [3/3] ACORN -- ACORN-1 Predicate-Aware Search"
echo "============================================================"
python3 "$BENCH" --mode acorn $COMMON_ARGS
echo ""

echo "============================================================"
echo " Comparison"
echo "============================================================"
python3 "$COMPARE" --results-dir "$OUT"

echo ""
echo "Done. JSON results are in $OUT/"
