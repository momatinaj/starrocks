# Spatial-Vector Benchmark Guide

This guide walks you through benchmarking the spatial-vector query improvements
developed in Phases 2 and 3.  Everything runs on the team server via SSH and
Docker -- no local build required.

---

## Quick Overview of the Three Modes

| Mode | Label | What it does | Cluster |
|------|-------|-------------|---------|
| **B0** | Brute Force | No vector index. Spatial filter first, then exact L2 distance on every survivor. | Custom (port 9030) |
| **A0** | Standard ANN | Official StarRocks. Uses vector index (ANN) and applies the spatial filter as a post-filter. | Baseline (port 19030) |
| **B2** | Planner Fallback | Custom build. The query planner detects the spatial+vector pattern and forces a smart fallback: spatial filter first, then exact distance on filtered rows. No ANN used. | Custom (port 9030) |

**What you should expect:**

- **B0 vs B2** -- B2 should be comparable on small result sets (both compute exact
  distances) but B2 benefits from planner awareness.
- **A0 vs B2** -- On highly selective spatial queries (small region, few candidates),
  B2 should win because exact distance on a small set is cheaper than an ANN
  scan of the full index followed by spatial post-filtering.  On broad spatial
  queries (large region, many candidates), A0 may win because ANN prunes the
  search space.

---

## Prerequisites

1. **SSH access** to the server where the repo is cloned.
2. **Docker and Docker Compose** installed on the server.
3. **Python 3.8+** on the server (for the benchmark script).
4. **pip packages**: `pymysql` and `numpy`.

```bash
pip3 install pymysql numpy
```

---

## Step 1 -- Clone / Update the repo

If you have not cloned yet:

```bash
git clone https://github.com/momatinaj/starrocks.git
cd starrocks
git checkout <branch-name>       # the branch with Phase 2/3 changes
```

If you already have the repo:

```bash
cd starrocks
git pull
```

---

## Step 2 -- Build FE and BE from the branch

The custom cluster (B2 mode) runs FE and BE compiled from your branch.
Build them inside Docker so you do not need any local toolchain:

```bash
# Generate protobuf/thrift sources (only needed once or after .proto/.thrift changes)
docker compose -f docker-compose.dev.yml run --rm gensrc

# Build the Frontend (Java)
docker compose -f docker-compose.dev.yml run --rm build-fe

# Build the Backend (C++)
docker compose -f docker-compose.dev.yml run --rm build-be
```

The build output lands in `output/fe/` and `output/be/` inside the repo.
Subsequent builds are fast thanks to ccache.

---

## Step 3 -- Start the clusters

### 3a. Start the A0 Baseline cluster (official StarRocks image)

```bash
docker compose -f docker-compose.dev.yml up -d starrocks-baseline
```

Wait ~30 seconds for it to initialise, then verify:

```bash
mysql -h 127.0.0.1 -P 19030 -u root -e "SHOW BACKENDS\G"
```

You should see one backend in the `Alive: true` state.

### 3b. Start the Custom cluster (B2 / A2 -- your branch)

```bash
docker compose -f docker-compose.dev.yml up -d starrocks-custom-fe starrocks-custom-be
```

The BE container will:
1. Wait for the FE to accept connections.
2. Register itself via `ALTER SYSTEM ADD BACKEND`.
3. Start the BE process.

Verify (wait ~40 seconds):

```bash
mysql -h 127.0.0.1 -P 9030 -u root -e "SHOW BACKENDS\G"
```

### Accessing StarRocks

Both clusters expose the standard MySQL protocol.  Connect with any MySQL
client:

| Cluster | Host | Port | User | Password |
|---------|------|------|------|----------|
| A0 Baseline | `127.0.0.1` | **19030** | `root` | *(none)* |
| Custom (B2) | `127.0.0.1` | **9030** | `root` | *(none)* |

Example:

```bash
mysql -h 127.0.0.1 -P 9030 -u root --prompt="SR-custom> "
```

---

## Step 4 -- Run the benchmarks

The benchmark script lives at:

```
research/06_s2_hnsw_execution_program/benchmarks/run_benchmark.py
```

### 4a. Generate ground truth (B0 -- brute force)

Run B0 first so you have exact results to measure recall against:

```bash
python3 research/06_s2_hnsw_execution_program/benchmarks/run_benchmark.py \
  --mode b0 \
  --port 9030 \
  --rows 100000 \
  --dim 128 \
  --k 10 \
  --queries 50 \
  --output research/06_s2_hnsw_execution_program/results/
```

This creates the table (without vector index), loads synthetic data, runs
queries, and saves ground-truth results.

### 4b. Run A0 (standard ANN on official image)

```bash
python3 research/06_s2_hnsw_execution_program/benchmarks/run_benchmark.py \
  --mode a0 \
  --port 19030 \
  --rows 100000 \
  --dim 128 \
  --k 10 \
  --queries 50 \
  --output research/06_s2_hnsw_execution_program/results/
```

### 4c. Run B2 (planner fallback on custom build)

```bash
python3 research/06_s2_hnsw_execution_program/benchmarks/run_benchmark.py \
  --mode b2 \
  --port 9030 \
  --rows 100000 \
  --dim 128 \
  --k 10 \
  --queries 50 \
  --output research/06_s2_hnsw_execution_program/results/
```

### What the script does

1. **Generates** synthetic geo-vector data: random lat/lng points clustered
   around San Francisco, New York, and Chicago with 128-dim random embeddings.
2. **Creates** a table (`bench_geo_vectors_<mode>`) with the right schema for
   the mode (with or without vector index).
3. **Loads** data via batch INSERT.
4. **Runs** a query set at multiple spatial selectivities (small radius,
   medium radius, large radius, polygon).
5. **Measures** latency (p50/p95/p99) and QPS.
6. **Computes recall** against B0 ground truth if results are available.
7. **Writes** a JSON results file to the output directory and prints a
   summary table.

---

## Step 5 -- Interpret the results

The script prints a summary table like:

```
Mode: b2  |  Rows: 100000  |  Dim: 128  |  K: 10
------------------------------------------------------
Query Type         | p50 ms | p95 ms | p99 ms | Recall
------------------------------------------------------
radius_1km         |   12.3 |   18.7 |   25.1 |  1.000
radius_5km         |   35.2 |   48.6 |   55.3 |  1.000
radius_20km        |  120.4 |  145.2 |  160.1 |  1.000
polygon_downtown   |   22.1 |   30.5 |   38.2 |  1.000
```

**Key metrics:**

- **p50/p95/p99 latency** -- lower is better.
- **Recall** -- fraction of ground-truth top-k IDs found by the mode.
  B2 should have recall = 1.0 (exact distance).  A0 recall depends on the
  ANN index quality and how well the spatial post-filter interacts with it.

**Expected findings:**

| Comparison | Selective queries | Broad queries |
|-----------|------------------|--------------|
| B2 vs A0 | B2 wins (filter first is cheap) | A0 may win (ANN prunes) |
| B2 vs B0 | Similar (both exact) | Similar |
| B2 recall | 1.0 (exact distances) | 1.0 |

---

## Step 6 -- Cleanup

Stop all benchmark containers:

```bash
docker compose -f docker-compose.dev.yml down
```

To also remove data volumes:

```bash
docker compose -f docker-compose.dev.yml down -v
```

To remove only one cluster:

```bash
docker compose -f docker-compose.dev.yml stop starrocks-baseline
docker compose -f docker-compose.dev.yml stop starrocks-custom-fe starrocks-custom-be
```

---

## Future: A2 (S2-Partitioned HNSW) Benchmarks

The A2 mode (Phase 3 spatial-partitioned vector index) is implemented and
validated via comprehensive unit tests:

- `S2CellUtilsTest` -- S2 cell utilities (28 tests)
- `SpatialPartitionMetaTest` -- partition metadata (28 tests)
- `SpatialVectorIndexWriterTest` -- partitioned index writer (10 tests)
- `SpatialVectorIndexReaderTest` -- partitioned index reader (11 tests)

Full end-to-end benchmarking of A2 requires the FE DDL integration (Phase 5)
so that `CREATE INDEX ... USING VECTOR(...)` can accept spatial partitioning
properties and resolve lat/lng column UIDs automatically.  Once that is done,
the benchmark script can be extended with `--mode a2` following the same
pattern.

You can run the unit tests now to verify correctness:

```bash
docker compose -f docker-compose.dev.yml run --rm test-be-spatial-vector
```

---

## Docker Compose Service Reference

| Service | Image | Purpose | Host Ports |
|---------|-------|---------|------------|
| `starrocks-baseline` | `starrocks/allin1-ubuntu:latest` | Official StarRocks (A0 baseline) | 19030, 18030, 18040 |
| `starrocks-custom-fe` | `starrocks/dev-env-ubuntu:latest` | FE from your branch | 9030, 8030 |
| `starrocks-custom-be` | `starrocks/dev-env-ubuntu:latest` | BE from your branch | 8040 |
| `build-fe` | `starrocks/dev-env-ubuntu:latest` | Build FE (run once) | -- |
| `build-be` | `starrocks/dev-env-ubuntu:latest` | Build BE (run once) | -- |
| `test-be-spatial-vector` | `starrocks/dev-env-ubuntu:latest` | Run all spatial-vector unit tests | -- |

---

## Troubleshooting

**FE does not start / "FE has not been built yet"**

Run `docker compose -f docker-compose.dev.yml run --rm build-fe` first.
Check that `output/fe/lib/` contains JAR files.

**BE does not register / "Waiting for FE" loops forever**

Check FE logs: `docker compose -f docker-compose.dev.yml logs starrocks-custom-fe`.
Common issue: FE needs ~20-30 seconds to become ready.

**Port conflict**

If ports 9030 or 19030 are already in use (e.g. another StarRocks instance),
edit `docker-compose.dev.yml` and change the host-side port numbers.

**mysql client not found on server**

Install it: `sudo apt-get install -y mariadb-client` (Ubuntu/Debian) or
`sudo yum install -y mariadb` (CentOS/RHEL).
