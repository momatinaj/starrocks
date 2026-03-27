#!/usr/bin/env bash
#
# End-to-end: pull, rebuild FE+BE, restart cluster, run full benchmark.
#
# Usage (run from project root):
#   ./research/06_s2_hnsw_execution_program/benchmarks/rebuild_and_benchmark.sh
#
# Skip options:
#   --skip-pull       Skip git pull
#   --skip-build-fe   Skip FE rebuild
#   --skip-build-be   Skip BE rebuild
#   --skip-build      Skip both FE and BE rebuild
#   --skip-clean      Skip storage/metadata cleanup
#   --skip-restart    Skip cluster stop/start/register (assume already running)
#   --skip-load       Skip data loading in benchmark (reuse existing tables)
#
# Benchmark options (passed through):
#   --rows 200000     Custom row count
#   --queries 100     Queries per spec
#   --warmup 10       Warmup queries
#

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
COMPOSE="docker-compose -f $PROJECT_ROOT/docker-compose.dev.yml"

SKIP_PULL=false
SKIP_FE=false
SKIP_BE=false
SKIP_CLEAN=false
SKIP_RESTART=false
BENCH_ARGS=""

for arg in "$@"; do
    case $arg in
        --skip-pull)      SKIP_PULL=true;;
        --skip-build-fe)  SKIP_FE=true;;
        --skip-build-be)  SKIP_BE=true;;
        --skip-build)     SKIP_FE=true; SKIP_BE=true;;
        --skip-clean)     SKIP_CLEAN=true;;
        --skip-restart)   SKIP_RESTART=true;;
        *)                BENCH_ARGS="$BENCH_ARGS $arg";;
    esac
done

step() { echo -e "\n\033[1;32m>> $1\033[0m\n"; }
warn() { echo -e "\033[1;33m   WARNING: $1\033[0m"; }
fail() { echo -e "\n\033[1;31mERROR: $1\033[0m\n"; exit 1; }

echo ""
echo "============================================================"
echo " Rebuild & Benchmark Pipeline"
echo "============================================================"
echo "  skip-pull=$SKIP_PULL  skip-build-fe=$SKIP_FE  skip-build-be=$SKIP_BE"
echo "  skip-clean=$SKIP_CLEAN  skip-restart=$SKIP_RESTART"
echo "  bench args: $BENCH_ARGS"
echo ""

# ---- 1. Git pull ----
if [ "$SKIP_PULL" = false ]; then
    step "Pulling latest code"
    cd "$PROJECT_ROOT"
    git pull || warn "git pull failed, continuing with current code"
else
    step "Skipping git pull (--skip-pull)"
fi

# ---- 2. Stop cluster ----
if [ "$SKIP_RESTART" = false ]; then
    step "Stopping cluster"
    $COMPOSE down starrocks-custom-fe starrocks-custom-be 2>/dev/null || true
fi

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
if [ "$SKIP_CLEAN" = false ]; then
    step "Cleaning old storage and metadata"
    sudo rm -rf "$PROJECT_ROOT/output/be/storage" "$PROJECT_ROOT/output/be/log"
    sudo rm -rf "$PROJECT_ROOT/output/fe/meta"
else
    step "Skipping storage cleanup (--skip-clean)"
fi

# ---- 6. Start cluster ----
if [ "$SKIP_RESTART" = false ]; then
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
        warn "Backend may not be ready yet. Waiting 30s more..."
        sleep 30
        BE_ALIVE=$(mysql -h 127.0.0.1 -P 9030 -u root -N -e \
            "SELECT Alive FROM information_schema.be_backends LIMIT 1;" 2>/dev/null || echo "")
        if [ "$BE_ALIVE" != "true" ]; then
            warn "Backend status: $BE_ALIVE -- proceeding anyway"
        else
            echo "  Backend is alive."
        fi
    fi
else
    step "Skipping cluster restart (--skip-restart)"
fi

# ---- 9. Run benchmark ----
step "Running full benchmark (B0 vs B2 vs ACORN-1)"
"$SCRIPT_DIR/run_full_benchmark.sh" $BENCH_ARGS || warn "Benchmark had errors (see above)"

step "All done!"
