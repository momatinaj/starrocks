#!/usr/bin/env bash
#
# End-to-end: pull, rebuild FE+BE, restart cluster, run full benchmark.
#
# Usage (run from project root):
#   ./research/06_s2_hnsw_execution_program/benchmarks/rebuild_and_benchmark.sh
#   ./research/06_s2_hnsw_execution_program/benchmarks/rebuild_and_benchmark.sh --skip-build-fe
#   ./research/06_s2_hnsw_execution_program/benchmarks/rebuild_and_benchmark.sh --skip-build-be
#   ./research/06_s2_hnsw_execution_program/benchmarks/rebuild_and_benchmark.sh --skip-load
#

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
COMPOSE="docker compose -f $PROJECT_ROOT/docker-compose.dev.yml"

SKIP_FE=false
SKIP_BE=false
BENCH_ARGS=""

for arg in "$@"; do
    case $arg in
        --skip-build-fe) SKIP_FE=true;;
        --skip-build-be) SKIP_BE=true;;
        *) BENCH_ARGS="$BENCH_ARGS $arg";;
    esac
done

step() { echo -e "\n\033[1;32m>> $1\033[0m\n"; }
fail() { echo -e "\n\033[1;31mERROR: $1\033[0m\n"; exit 1; }

# ---- 1. Git pull ----
step "Pulling latest code"
cd "$PROJECT_ROOT"
git pull || fail "git pull failed"

# ---- 2. Stop cluster ----
step "Stopping cluster"
$COMPOSE down starrocks-custom-fe starrocks-custom-be 2>/dev/null || true

# ---- 3. Build FE ----
if [ "$SKIP_FE" = false ]; then
    step "Building FE (this may take a few minutes)"
    $COMPOSE run --rm build-fe || fail "FE build failed"
else
    step "Skipping FE build (--skip-build-fe)"
fi

# ---- 4. Build BE ----
if [ "$SKIP_BE" = false ]; then
    step "Building BE (this may take several minutes)"
    $COMPOSE run --rm build-be || fail "BE build failed"
else
    step "Skipping BE build (--skip-build-be)"
fi

# ---- 5. Clean storage ----
step "Cleaning old storage and metadata"
rm -rf "$PROJECT_ROOT/output/be/storage" "$PROJECT_ROOT/output/be/log"
rm -rf "$PROJECT_ROOT/output/fe/meta"

# ---- 6. Start cluster ----
step "Starting FE and BE"
$COMPOSE up -d starrocks-custom-fe starrocks-custom-be

echo "  Waiting for FE to start (45s) ..."
sleep 45

# ---- 7. Register BE ----
step "Registering BE with FE"
mysql -h 127.0.0.1 -P 9030 -u root -e \
    "ALTER SYSTEM ADD BACKEND 'starrocks-custom-be:9050';" 2>/dev/null || true

echo "  Waiting for BE heartbeat (20s) ..."
sleep 20

# ---- 8. Verify ----
step "Verifying cluster health"
BE_ALIVE=$(mysql -h 127.0.0.1 -P 9030 -u root -N -e \
    "SELECT Alive FROM information_schema.be_backends LIMIT 1;" 2>/dev/null || echo "")
if [ "$BE_ALIVE" = "true" ]; then
    echo "  Backend is alive."
else
    echo "  WARNING: Backend may not be ready yet. Waiting 30s more..."
    sleep 30
    BE_ALIVE=$(mysql -h 127.0.0.1 -P 9030 -u root -N -e \
        "SELECT Alive FROM information_schema.be_backends LIMIT 1;" 2>/dev/null || echo "")
    if [ "$BE_ALIVE" != "true" ]; then
        echo "  Backend status: $BE_ALIVE"
        echo "  Proceeding anyway (benchmark will fail if BE is not ready)."
    else
        echo "  Backend is alive."
    fi
fi

# ---- 9. Run benchmark ----
step "Running full benchmark (B0 vs B2 vs ACORN-1)"
"$SCRIPT_DIR/run_full_benchmark.sh" $BENCH_ARGS

step "All done!"
