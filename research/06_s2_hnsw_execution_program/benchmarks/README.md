# Spatial-Vector Benchmarks

Quick-start reference for running the spatial-vector query benchmarks.

## Benchmark Modes

| Mode    | Description                                         | Index Type | Port  |
|---------|-----------------------------------------------------|------------|-------|
| `b0`    | Brute force -- no vector index. Ground truth.       | none       | 9030  |
| `a0`    | Official StarRocks release, standard HNSW ANN.      | hnsw       | 19030 |
| `b2`    | Custom build, planner fallback (spatial + exact).   | hnsw       | 9030  |
| `acorn` | Custom build, ACORN-1 predicate-aware HNSW search.  | acorn      | 9030  |

## Install Dependencies

```bash
pip3 install pymysql numpy matplotlib
```

## Start the Clusters

```bash
# Official StarRocks (A0 baseline) on port 19030
docker compose -f docker-compose.dev.yml up -d starrocks-baseline

# Custom-built StarRocks (B2 / ACORN) on port 9030
docker compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be
```

Wait ~40 seconds, then verify both clusters are healthy:

```bash
mysql -h 127.0.0.1 -P 19030 -u root -e "SHOW BACKENDS\G"
mysql -h 127.0.0.1 -P 9030  -u root -e "SHOW BACKENDS\G"
```

## Run All Four Modes

```bash
BENCH=research/06_s2_hnsw_execution_program/benchmarks/run_benchmark.py
OUT=research/06_s2_hnsw_execution_program/benchmarks/results/

# 1. Ground truth (brute force, no vector index)
python3 $BENCH --mode b0 --port 9030 --rows 100000 --output $OUT

# 2. A0 baseline (official StarRocks, standard ANN)
python3 $BENCH --mode a0 --port 19030 --rows 100000 --output $OUT

# 3. B2 planner fallback (custom build, spatial-aware)
python3 $BENCH --mode b2 --port 9030 --rows 100000 --output $OUT

# 4. ACORN-1 predicate-aware search (custom build)
python3 $BENCH --mode acorn --port 9030 --rows 100000 --output $OUT
```

## Rerun Queries Without Reloading Data

```bash
python3 $BENCH --mode acorn --port 9030 --rows 100000 --skip-load --output $OUT
```

## Compare All Results

After running multiple modes, generate a side-by-side comparison:

```bash
python3 research/06_s2_hnsw_execution_program/benchmarks/compare_results.py \
    --results-dir $OUT
```

This prints a table with p50/p95/p99 latency, recall, and speedup vs brute force
for each query type across all available modes.

## One-Command Full Benchmark

Run all three modes (B0, B2, ACORN) and generate a comparison report:

```bash
./research/06_s2_hnsw_execution_program/benchmarks/run_full_benchmark.sh
```

Options: `--rows 200000`, `--queries 100`, `--warmup 10`, `--skip-load`.

## Generate Charts & Report

After running benchmarks, generate an HTML report with charts:

```bash
python3 research/06_s2_hnsw_execution_program/benchmarks/generate_report.py \
    --results-dir $OUT
```

Opens a self-contained HTML file with latency charts, recall comparison,
speedup analysis, and box plots.

## Results

JSON files are written to the `--output` directory. The script also prints a
summary table with p50/p95/p99 latency and recall per query type.

## Cleanup

```bash
docker compose -f docker-compose.dev.yml down
```
