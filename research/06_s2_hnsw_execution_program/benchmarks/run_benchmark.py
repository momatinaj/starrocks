#!/usr/bin/env python3
"""
Spatial-Vector Benchmark Runner for StarRocks.

Generates synthetic geo+vector data, loads it into StarRocks, executes
spatial+vector queries at various selectivities, and reports latency / recall.

Usage:
    python3 run_benchmark.py --mode b0    --port 9030  --rows 100000
    python3 run_benchmark.py --mode a0    --port 19030 --rows 100000
    python3 run_benchmark.py --mode b2    --port 9030  --rows 100000
    python3 run_benchmark.py --mode acorn --port 9030  --rows 100000

Modes:
    b0          - Brute force (no vector index). Ground truth for recall.
    a0          - Official StarRocks release with standard HNSW ANN.
    b2          - Custom build with planner fallback (SPATIAL_FILTER + EXACT_DISTANCE).
    acorn       - Custom build with ACORN-1 predicate-aware HNSW search.
    acorn_gamma - Custom build with ACORN-gamma (denser graph, gamma*M neighbors).

Requirements:
    pip3 install pymysql numpy
"""

import argparse
import json
import os
import sys
import time
from datetime import datetime

import numpy as np
import pymysql

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DB_NAME = "bench_spatial_vector"

CITY_CENTERS = [
    ("san_francisco", 37.7749, -122.4194),
    ("new_york", 40.7128, -74.0060),
    ("chicago", 41.8781, -87.6298),
]

QUERY_SPECS = [
    {
        "name": "radius_1km",
        "description": "1 km radius around city center (very selective)",
        "radius_m": 1000,
        "type": "radius",
    },
    {
        "name": "radius_5km",
        "description": "5 km radius (moderately selective)",
        "radius_m": 5000,
        "type": "radius",
    },
    {
        "name": "radius_20km",
        "description": "20 km radius (broad)",
        "radius_m": 20000,
        "type": "radius",
    },
    {
        "name": "polygon_downtown",
        "description": "Small downtown polygon (~0.02 x 0.02 degrees)",
        "type": "polygon",
        "half_side_deg": 0.01,
    },
    {
        "name": "polygon_metro",
        "description": "Metro-area polygon (~0.2 x 0.2 degrees)",
        "type": "polygon",
        "half_side_deg": 0.1,
    },
]


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------


def get_connection(host, port):
    return pymysql.connect(
        host=host,
        port=port,
        user="root",
        password="",
        charset="utf8mb4",
        connect_timeout=30,
        read_timeout=300,
    )


def execute(conn, sql, fetch=False):
    with conn.cursor() as cur:
        cur.execute(sql)
        if fetch:
            return cur.fetchall()
    conn.commit()


def table_name(mode):
    return f"bench_geo_vectors_{mode}"


# ---------------------------------------------------------------------------
# Data Generation
# ---------------------------------------------------------------------------


def generate_data(rows, dim, seed=42):
    """Generate synthetic geo-vector rows clustered around city centers."""
    rng = np.random.default_rng(seed)

    n_cities = len(CITY_CENTERS)
    per_city = rows // n_cities
    remainder = rows - per_city * n_cities

    all_lat = []
    all_lng = []
    all_vecs = []

    for i, (_, c_lat, c_lng) in enumerate(CITY_CENTERS):
        n = per_city + (1 if i < remainder else 0)
        lats = c_lat + rng.normal(0, 0.05, n)
        lngs = c_lng + rng.normal(0, 0.05, n)
        vecs = rng.standard_normal((n, dim)).astype(np.float32)
        all_lat.append(lats)
        all_lng.append(lngs)
        all_vecs.append(vecs)

    all_lat = np.concatenate(all_lat)
    all_lng = np.concatenate(all_lng)
    all_vecs = np.concatenate(all_vecs)

    perm = rng.permutation(rows)
    return all_lat[perm], all_lng[perm], all_vecs[perm]


def vec_to_sql(v):
    """Format a numpy vector as a StarRocks ARRAY literal."""
    return "[" + ",".join(f"{x:.6f}" for x in v) + "]"


# ---------------------------------------------------------------------------
# Table Setup
# ---------------------------------------------------------------------------

DDL_BASE = """
CREATE TABLE IF NOT EXISTS {table} (
    id          BIGINT          NOT NULL,
    lat         DOUBLE          NOT NULL,
    lng         DOUBLE          NOT NULL,
    embedding   ARRAY<FLOAT>    NOT NULL
    {index_clause}
) ENGINE=OLAP
DUPLICATE KEY(id)
DISTRIBUTED BY HASH(id) BUCKETS 4
PROPERTIES ("replication_num" = "1");
"""

HNSW_INDEX_CLAUSE = """,
    INDEX vec_idx (embedding) USING VECTOR(
        "index_type" = "hnsw",
        "dim" = "{dim}",
        "metric_type" = "l2_distance",
        "is_vector_normed" = "false",
        "M" = "16",
        "efconstruction" = "40"
    )"""

ACORN_INDEX_CLAUSE = """,
    INDEX vec_idx (embedding) USING VECTOR(
        "index_type" = "acorn",
        "dim" = "{dim}",
        "metric_type" = "l2_distance",
        "is_vector_normed" = "false",
        "M" = "16",
        "efconstruction" = "40"
    )"""

ACORN_GAMMA_INDEX_CLAUSE = """,
    INDEX vec_idx (embedding) USING VECTOR(
        "index_type" = "acorn_gamma",
        "dim" = "{dim}",
        "metric_type" = "l2_distance",
        "is_vector_normed" = "false",
        "M" = "16",
        "efconstruction" = "40",
        "gamma" = "{gamma}"
    )"""

GRID_HNSW_INDEX_CLAUSE = """,
    INDEX vec_idx (embedding) USING VECTOR(
        "index_type" = "grid_hnsw",
        "dim" = "{dim}",
        "metric_type" = "l2_distance",
        "is_vector_normed" = "false",
        "M" = "16",
        "efconstruction" = "40",
        "s2_level" = "12",
        "lat_column" = "lat",
        "lng_column" = "lng"{grid_extras}
    )"""

VECTOR_INDEX_CLAUSE = HNSW_INDEX_CLAUSE


def create_table(conn, mode, dim, gamma=2, grid_extras=""):
    tbl = table_name(mode)
    execute(conn, f"DROP TABLE IF EXISTS {tbl}")

    if mode == "b0":
        idx = ""
    elif mode == "acorn":
        idx = ACORN_INDEX_CLAUSE.format(dim=dim)
    elif mode.startswith("acorn_gamma"):
        idx = ACORN_GAMMA_INDEX_CLAUSE.format(dim=dim, gamma=gamma)
    elif mode.startswith("grid"):
        idx = GRID_HNSW_INDEX_CLAUSE.format(dim=dim, grid_extras=grid_extras)
    else:
        idx = HNSW_INDEX_CLAUSE.format(dim=dim)

    ddl = DDL_BASE.format(table=tbl, index_clause=idx)
    execute(conn, ddl)
    print(f"  Created table {tbl}")


# ---------------------------------------------------------------------------
# Data Loading
# ---------------------------------------------------------------------------

BATCH_SIZE = 500


def load_data(conn, mode, lats, lngs, vecs):
    tbl = table_name(mode)
    rows = len(lats)
    loaded = 0
    t0 = time.time()

    for start in range(0, rows, BATCH_SIZE):
        end = min(start + BATCH_SIZE, rows)
        values = []
        for i in range(start, end):
            v_str = vec_to_sql(vecs[i])
            values.append(f"({i}, {lats[i]:.8f}, {lngs[i]:.8f}, {v_str})")
        sql = f"INSERT INTO {tbl} (id, lat, lng, embedding) VALUES " + ",".join(values)
        execute(conn, sql)
        loaded = end
        elapsed = time.time() - t0
        rate = loaded / elapsed if elapsed > 0 else 0
        print(
            f"\r  Loaded {loaded}/{rows} rows ({rate:.0f} rows/s)", end="", flush=True
        )

    print()
    elapsed = time.time() - t0
    print(f"  Loading complete: {rows} rows in {elapsed:.1f}s")


# ---------------------------------------------------------------------------
# Query Execution
# ---------------------------------------------------------------------------


def build_radius_query(tbl, center_lat, center_lng, radius_m, query_vec, k):
    return f"""
SELECT id,
       approx_l2_distance(embedding, {vec_to_sql(query_vec)}) AS dist
FROM {tbl}
WHERE ST_Distance_Sphere(lng, lat, {center_lng}, {center_lat}) < {radius_m}
ORDER BY dist
LIMIT {k}
"""


def build_polygon_query(tbl, center_lat, center_lng, half_side, query_vec, k):
    min_lat = center_lat - half_side
    max_lat = center_lat + half_side
    min_lng = center_lng - half_side
    max_lng = center_lng + half_side
    wkt = (
        f"POLYGON(({min_lng} {min_lat}, {max_lng} {min_lat}, "
        f"{max_lng} {max_lat}, {min_lng} {max_lat}, {min_lng} {min_lat}))"
    )
    return f"""
SELECT id,
       approx_l2_distance(embedding, {vec_to_sql(query_vec)}) AS dist
FROM {tbl}
WHERE ST_Contains(
    ST_GeomFromText('{wkt}'),
    ST_Point(lng, lat)
)
ORDER BY dist
LIMIT {k}
"""


def run_single_query(conn, sql):
    """Execute a single query and return (result_rows, latency_ms)."""
    t0 = time.perf_counter()
    with conn.cursor() as cur:
        cur.execute(sql)
        rows = cur.fetchall()
    latency = (time.perf_counter() - t0) * 1000
    return rows, latency


def run_query_set(conn, mode, query_vecs, k, num_queries, warmup=5):
    """Run all query specs against multiple city centers and query vectors."""
    tbl = table_name(mode)
    results = {}

    for spec in QUERY_SPECS:
        spec_name = spec["name"]
        latencies = []
        all_result_ids = []

        if warmup > 0:
            print(f"  {spec_name}: running {warmup} warmup queries...")
            for wi in range(warmup):
                qvec = query_vecs[wi % len(query_vecs)]
                city_idx = wi % len(CITY_CENTERS)
                _, c_lat, c_lng = CITY_CENTERS[city_idx]
                if spec["type"] == "radius":
                    sql = build_radius_query(tbl, c_lat, c_lng, spec["radius_m"], qvec, k)
                else:
                    sql = build_polygon_query(tbl, c_lat, c_lng, spec["half_side_deg"], qvec, k)
                run_single_query(conn, sql)

        for qi in range(min(num_queries, len(query_vecs))):
            qvec = query_vecs[qi]
            city_idx = qi % len(CITY_CENTERS)
            _, c_lat, c_lng = CITY_CENTERS[city_idx]

            if spec["type"] == "radius":
                sql = build_radius_query(tbl, c_lat, c_lng, spec["radius_m"], qvec, k)
            else:
                sql = build_polygon_query(
                    tbl, c_lat, c_lng, spec["half_side_deg"], qvec, k
                )

            rows, latency = run_single_query(conn, sql)
            latencies.append(latency)
            result_ids = [int(r[0]) for r in rows]
            all_result_ids.append(result_ids)

        latencies_arr = np.array(latencies)
        results[spec_name] = {
            "description": spec["description"],
            "latencies_ms": latencies,
            "p50_ms": float(np.percentile(latencies_arr, 50)),
            "p95_ms": float(np.percentile(latencies_arr, 95)),
            "p99_ms": float(np.percentile(latencies_arr, 99)),
            "qps": (
                1000.0 / float(np.mean(latencies_arr))
                if np.mean(latencies_arr) > 0
                else 0
            ),
            "result_ids": all_result_ids,
            "num_queries": len(latencies),
        }
        print(
            f"  {spec_name}: p50={results[spec_name]['p50_ms']:.1f}ms "
            f"p95={results[spec_name]['p95_ms']:.1f}ms "
            f"p99={results[spec_name]['p99_ms']:.1f}ms"
        )

    return results


# ---------------------------------------------------------------------------
# Recall Computation
# ---------------------------------------------------------------------------


def compute_recall(ground_truth_ids, candidate_ids):
    """Recall@K: fraction of ground-truth IDs found in candidate results."""
    if not ground_truth_ids:
        return None
    recalls = []
    for gt, cand in zip(ground_truth_ids, candidate_ids):
        gt_set = set(gt)
        if not gt_set:
            continue
        hit = len(gt_set & set(cand))
        recalls.append(hit / len(gt_set))
    return float(np.mean(recalls)) if recalls else None


def load_ground_truth(output_dir):
    """Load B0 results as ground truth if available."""
    gt_files = sorted(
        [
            f
            for f in os.listdir(output_dir)
            if f.startswith("b0_") and f.endswith(".json")
        ],
        reverse=True,
    )
    if not gt_files:
        return None
    path = os.path.join(output_dir, gt_files[0])
    with open(path) as f:
        return json.load(f)


# ---------------------------------------------------------------------------
# Reporting
# ---------------------------------------------------------------------------


MODE_DESCRIPTIONS = {
    "b0": "Brute Force (Ground Truth)",
    "a0": "Official StarRocks ANN (HNSW)",
    "b2": "Custom Planner Fallback (SPATIAL_FILTER + EXACT_DISTANCE)",
    "acorn": "ACORN-1 Predicate-Aware HNSW",
    "acorn_gamma": "ACORN-gamma Dense-Graph HNSW (gamma=2)",
    "grid": "Grid-HNSW (Spatially Partitioned HNSW)",
    "grid_g1": "Grid-HNSW + G1 (Oversample)",
    "grid_g2": "Grid-HNSW + G2 (Neighbor Expansion)",
    "grid_g3": "Grid-HNSW + G3 (Small-Cell Scan)",
    "grid_g4": "Grid-HNSW + G4 (Max Cover Cells)",
    "grid_g1_g2": "Grid-HNSW + G1+G2 (Oversample + Neighbors)",
    "grid_g1_g2_g3": "Grid-HNSW + G1+G2+G3 (All except G4)",
    "grid_g1_g2_g3_g4": "Grid-HNSW + All Improvements",
}


def print_summary(mode, rows, dim, k, results, recalls):
    desc = MODE_DESCRIPTIONS.get(mode, mode)
    print()
    print(f"Mode: {mode} -- {desc}")
    print(f"Rows: {rows}  |  Dim: {dim}  |  K: {k}")
    print("-" * 68)
    header = f"{'Query Type':<20} | {'p50 ms':>8} | {'p95 ms':>8} | {'p99 ms':>8} | {'Recall':>7}"
    print(header)
    print("-" * 68)
    for spec_name, data in results.items():
        recall_str = (
            f"{recalls[spec_name]:.3f}"
            if recalls.get(spec_name) is not None
            else "  n/a"
        )
        print(
            f"{spec_name:<20} | {data['p50_ms']:>8.1f} | {data['p95_ms']:>8.1f} | "
            f"{data['p99_ms']:>8.1f} | {recall_str:>7}"
        )
    print("-" * 68)
    print()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------


def main():
    parser = argparse.ArgumentParser(description="Spatial-Vector Benchmark Runner")
    parser.add_argument(
        "--mode",
        required=True,
        choices=["b0", "a0", "b2", "acorn", "acorn_gamma", "grid"],
        help="Benchmark mode: b0 (brute force), a0 (official ANN), b2 (planner fallback), acorn (ACORN-1), acorn_gamma (ACORN-gamma), grid (Grid-HNSW)",
    )
    parser.add_argument(
        "--host", default="127.0.0.1", help="StarRocks host (default: 127.0.0.1)"
    )
    parser.add_argument("--port", type=int, default=9030, help="StarRocks query port")
    parser.add_argument(
        "--rows", type=int, default=100000, help="Number of data rows to generate"
    )
    parser.add_argument("--dim", type=int, default=128, help="Vector dimension")
    parser.add_argument("--k", type=int, default=10, help="Top-K for queries")
    parser.add_argument(
        "--queries", type=int, default=50, help="Number of queries per spec"
    )
    parser.add_argument(
        "--seed", type=int, default=42, help="Random seed for reproducibility"
    )
    parser.add_argument(
        "--output", default="results/", help="Output directory for results JSON"
    )
    parser.add_argument(
        "--warmup",
        type=int,
        default=5,
        help="Number of warmup queries per spec before timing (default: 5)",
    )
    parser.add_argument(
        "--skip-load",
        action="store_true",
        help="Skip data generation and loading (reuse existing table)",
    )
    parser.add_argument(
        "--clone-from",
        default=None,
        help="Clone data from an existing mode's table (e.g. --clone-from b0) instead of row-by-row INSERT",
    )
    parser.add_argument(
        "--gamma",
        type=int,
        default=2,
        help="Gamma value for acorn_gamma mode (default: 2). Ignored for other modes.",
    )
    parser.add_argument(
        "--grid-oversample",
        type=float,
        default=1.0,
        help="Grid-HNSW G1: per-partition oversample factor (default: 1.0 = off). Ignored for non-grid modes.",
    )
    parser.add_argument(
        "--grid-expand-neighbors",
        action="store_true",
        help="Grid-HNSW G2: expand S2 covering with edge-neighbor cells.",
    )
    parser.add_argument(
        "--grid-scan-small",
        action="store_true",
        help="Grid-HNSW G3: include rows from small cells (no HNSW) as candidates.",
    )
    parser.add_argument(
        "--grid-max-cells",
        type=int,
        default=500,
        help="Grid-HNSW G4: max S2 cover cells (default: 500).",
    )
    args = parser.parse_args()

    resolved_mode = args.mode
    if args.mode == "acorn_gamma":
        resolved_mode = f"acorn_gamma_{args.gamma}"
    elif args.mode == "grid":
        # Build a suffix from active grid improvements for ablation tracking
        grid_tags = []
        if args.grid_oversample > 1.0:
            grid_tags.append("g1")
        if args.grid_expand_neighbors:
            grid_tags.append("g2")
        if args.grid_scan_small:
            grid_tags.append("g3")
        if args.grid_max_cells != 500:
            grid_tags.append("g4")
        if grid_tags:
            resolved_mode = f"grid_{'_'.join(grid_tags)}"

    os.makedirs(args.output, exist_ok=True)

    gamma_str = f"  |  Gamma: {args.gamma}" if args.mode == "acorn_gamma" else ""
    grid_str = ""
    if args.mode == "grid" and resolved_mode != "grid":
        grid_str = f"  |  Grid improvements: {', '.join(resolved_mode.split('_')[1:])}"
    print(f"\n{'='*60}")
    print(f"Spatial-Vector Benchmark  --  Mode: {resolved_mode.upper()}")
    print(f"{'='*60}")
    print(f"  Host: {args.host}:{args.port}")
    print(f"  Rows: {args.rows}  |  Dim: {args.dim}  |  K: {args.k}{gamma_str}{grid_str}")
    print(f"  Queries per spec: {args.queries}  |  Warmup: {args.warmup}")
    print()

    conn = get_connection(args.host, args.port)

    # Create database
    execute(conn, f"CREATE DATABASE IF NOT EXISTS {DB_NAME}")
    execute(conn, f"USE {DB_NAME}")

    if args.mode in ("b2", "a0", "acorn", "acorn_gamma", "grid"):
        print("  Enabling vector index feature...")
        execute(
            conn, 'ADMIN SET FRONTEND CONFIG ("enable_experimental_vector" = "true")'
        )

    # Build grid DDL extras from improvement flags
    grid_extras_parts = []
    if args.mode == "grid":
        if args.grid_oversample > 1.0:
            grid_extras_parts.append(f',\n        "grid_oversample" = "{args.grid_oversample}"')
        if args.grid_expand_neighbors:
            grid_extras_parts.append(',\n        "grid_expand_neighbors" = "true"')
        if args.grid_scan_small:
            grid_extras_parts.append(',\n        "grid_scan_small_cells" = "true"')
        if args.grid_max_cells != 500:
            grid_extras_parts.append(f',\n        "grid_max_cover_cells" = "{args.grid_max_cells}"')
    grid_extras = "".join(grid_extras_parts)

    need_load = not args.skip_load
    if args.skip_load:
        tbl = table_name(resolved_mode)
        try:
            execute(conn, f"SELECT 1 FROM {tbl} LIMIT 1", fetch=True)
            print("[1-3/4] Skipped (--skip-load). Reusing existing table.")
        except Exception:
            print(f"  Table {tbl} does not exist. Loading data despite --skip-load.")
            need_load = True

    if need_load and args.clone_from:
        src = table_name(args.clone_from)
        tbl = table_name(resolved_mode)
        print(f"[2/4] Creating table {tbl} ...")
        create_table(conn, resolved_mode, args.dim, gamma=args.gamma, grid_extras=grid_extras)
        print(f"[3/4] Cloning data: INSERT INTO {tbl} SELECT * FROM {src} ...")
        t0 = time.time()
        execute(conn, f"INSERT INTO {tbl} SELECT * FROM {src}")
        elapsed = time.time() - t0
        print(f"  Clone complete in {elapsed:.1f}s")
        print("  Waiting for data to settle...")
        time.sleep(5)
    elif need_load:
        print("[1/4] Generating synthetic data...")
        lats, lngs, vecs = generate_data(args.rows, args.dim, args.seed)

        print("[2/4] Creating table...")
        create_table(conn, resolved_mode, args.dim, gamma=args.gamma, grid_extras=grid_extras)

        print("[3/4] Loading data...")
        load_data(conn, resolved_mode, lats, lngs, vecs)

        print("  Waiting for data to settle...")
        time.sleep(5)

    # Generate query vectors (use a different seed so queries differ from data)
    query_rng = np.random.default_rng(args.seed + 1000)
    query_vecs = query_rng.standard_normal((args.queries, args.dim)).astype(np.float32)

    # Run queries
    print("[4/4] Running queries...")
    results = run_query_set(conn, resolved_mode, query_vecs, args.k, args.queries, args.warmup)

    # Compute recall against B0 ground truth
    recalls = {}
    gt = load_ground_truth(args.output)
    if gt and args.mode != "b0":
        gt_results = gt.get("results", {})
        for spec_name, data in results.items():
            gt_spec = gt_results.get(spec_name, {})
            gt_ids = gt_spec.get("result_ids", [])
            if gt_ids:
                recalls[spec_name] = compute_recall(gt_ids, data["result_ids"])
        print("  Recall computed against B0 ground truth.")
    elif args.mode == "b0":
        for spec_name in results:
            recalls[spec_name] = 1.0
        print("  B0 mode: recall is 1.0 by definition (ground truth).")
    else:
        print("  No B0 ground truth found. Run B0 first for recall computation.")

    # Summary
    mode_desc = f"ACORN-gamma (gamma={args.gamma})" if args.mode == "acorn_gamma" else MODE_DESCRIPTIONS.get(resolved_mode, resolved_mode)
    print_summary(resolved_mode, args.rows, args.dim, args.k, results, recalls)

    # Save results
    timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    out_file = os.path.join(args.output, f"{resolved_mode}_{timestamp}.json")

    output_data = {
        "mode": resolved_mode,
        "mode_description": mode_desc,
        "host": args.host,
        "port": args.port,
        "rows": args.rows,
        "dim": args.dim,
        "k": args.k,
        "num_queries": args.queries,
        "seed": args.seed,
        "gamma": args.gamma if args.mode == "acorn_gamma" else None,
        "grid_oversample": args.grid_oversample if args.mode == "grid" and args.grid_oversample > 1.0 else None,
        "grid_expand_neighbors": args.grid_expand_neighbors if args.mode == "grid" else None,
        "grid_scan_small_cells": args.grid_scan_small if args.mode == "grid" else None,
        "grid_max_cover_cells": args.grid_max_cells if args.mode == "grid" and args.grid_max_cells != 500 else None,
        "timestamp": timestamp,
        "results": {},
        "recalls": recalls,
    }
    for spec_name, data in results.items():
        output_data["results"][spec_name] = {
            "description": data["description"],
            "p50_ms": data["p50_ms"],
            "p95_ms": data["p95_ms"],
            "p99_ms": data["p99_ms"],
            "qps": data["qps"],
            "num_queries": data["num_queries"],
            "result_ids": data["result_ids"],
        }

    with open(out_file, "w") as f:
        json.dump(output_data, f, indent=2)
    print(f"Results saved to {out_file}")

    conn.close()


if __name__ == "__main__":
    main()
