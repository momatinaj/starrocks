# Spatial-Vector Benchmarks

Quick-start reference for running the spatial-vector query benchmarks.
See [BENCHMARK_GUIDE.md](../BENCHMARK_GUIDE.md) for the full walkthrough
(server setup, Docker cluster startup, result interpretation).

## Install Dependencies

```bash
pip3 install pymysql numpy
```

## Start the Clusters

```bash
# Official StarRocks (A0 baseline) on port 19030
docker compose -f docker-compose.dev.yml up -d starrocks-baseline

# Custom-built StarRocks (B2 planner fallback) on port 9030
docker compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be
```

Wait ~40 seconds, then verify both clusters are healthy:

```bash
mysql -h 127.0.0.1 -P 19030 -u root -e "SHOW BACKENDS\G"
mysql -h 127.0.0.1 -P 9030  -u root -e "SHOW BACKENDS\G"
```

## Run All Three Modes

```bash
BENCH=research/06_s2_hnsw_execution_program/benchmarks/run_benchmark.py
OUT=research/06_s2_hnsw_execution_program/results/

# 1. Ground truth (brute force, no vector index)
python3 $BENCH --mode b0 --port 9030 --rows 100000 --output $OUT

# 2. A0 baseline (official StarRocks, standard ANN)
python3 $BENCH --mode a0 --port 19030 --rows 100000 --output $OUT

# 3. B2 planner fallback (custom build, spatial-aware)
python3 $BENCH --mode b2 --port 9030 --rows 100000 --output $OUT
```

## Rerun Queries Without Reloading Data

```bash
python3 $BENCH --mode b2 --port 9030 --rows 100000 --skip-load --output $OUT
```

## Results

JSON files are written to the `--output` directory.  The script also prints a
summary table with p50/p95/p99 latency and recall per query type.

## Cleanup

```bash
docker compose -f docker-compose.dev.yml down
```
