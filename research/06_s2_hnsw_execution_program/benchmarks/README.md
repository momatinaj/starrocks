# Spatial-Vector Benchmarks

Quick-start reference for running the spatial-vector query benchmarks.

## Benchmark Modes

| Mode    | Description                                         | Index Type  | Port  |
|---------|-----------------------------------------------------|-------------|-------|
| `b0`    | Brute force -- no vector index. Ground truth.       | none        | 9030  |
| `acorn` | ACORN-1 predicate-aware HNSW search.                | acorn       | 9030  |
| `grid`  | Grid-HNSW spatially partitioned search.             | grid_hnsw   | 9030  |

Legacy modes (manual use only, not in default pipeline):

| Mode    | Description                                         | Index Type  | Port  |
|---------|-----------------------------------------------------|-------------|-------|
| `a0`    | Official StarRocks release, standard HNSW ANN.      | hnsw        | 19030 |
| `b2`    | Custom build, planner fallback (spatial + exact).   | hnsw        | 9030  |

## Install Dependencies

```bash
pip3 install pymysql numpy matplotlib
```

## Start the Cluster

```bash
docker compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be
```

Wait ~40 seconds, then verify:

```bash
mysql -h 127.0.0.1 -P 9030 -u root -e "SHOW BACKENDS\G"
```

## One-Command Full Benchmark

Run B0 (brute force) vs ACORN-1 vs Grid-HNSW and generate a comparison report:

```bash
./run_full_benchmark.sh
```

### Options

```bash
./run_full_benchmark.sh --rows 200000       # custom row count
./run_full_benchmark.sh --skip-load         # reuse existing tables
./run_full_benchmark.sh --skip-baseline     # skip B0 (uses cached results)
./run_full_benchmark.sh --only acorn        # run only ACORN-1
./run_full_benchmark.sh --only grid         # run only Grid-HNSW
./run_full_benchmark.sh --skip-load --skip-baseline  # fastest re-run
```

### When to Reload Data

| Situation | Need `--skip-load`? |
|-----------|-------------------|
| First run ever | No (data generated automatically) |
| Changed `--rows` or `--dim` | No (must reload) |
| Code changes only | Yes |
| Changed index params (M, ef) | No (must rebuild) |
| Same server, later session | Yes |

## Run Individual Modes

```bash
BENCH=run_benchmark.py
OUT=results/

python3 $BENCH --mode b0    --port 9030 --rows 100000 --output $OUT
python3 $BENCH --mode acorn --port 9030 --rows 100000 --output $OUT --clone-from b0
python3 $BENCH --mode grid  --port 9030 --rows 100000 --output $OUT --clone-from b0
```

Use `--skip-load` to rerun queries without reloading data:

```bash
python3 $BENCH --mode acorn --port 9030 --rows 100000 --skip-load --output $OUT
```

## Compare Results

```bash
python3 compare_results.py --results-dir results/
```

## Generate HTML Report

```bash
python3 generate_report.py --results-dir results/ --output results/benchmark_report.html
```

## Results

JSON files are written to the `--output` directory. The comparison script prints a table with p50/p95 latency, recall, and speedup vs brute force for each query type.

## Cleanup

```bash
docker compose -f docker-compose.dev.yml down
```
