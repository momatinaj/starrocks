#!/usr/bin/env python3
"""
Compare benchmark results across modes (B0, ACORN-1, ACORN-gamma, Grid-HNSW).

Reads the latest JSON result file for each mode from the results directory
and prints a side-by-side comparison table with latency and recall.

Usage:
    python3 compare_results.py                          # default results/ dir
    python3 compare_results.py --results-dir my_results/
"""

import argparse
import json
import os
import sys
from collections import defaultdict

BASE_MODE_ORDER = ["b0", "acorn", "grid"]
MODE_LABELS = {
    "b0": "B0 Brute Force",
    "acorn": "ACORN-1",
    "grid": "Grid-HNSW",
}


def discover_modes(results_dir):
    """Scan the results directory and return full mode order + labels with any
    acorn_gamma_N and grid_gN variants discovered dynamically."""
    import re

    gamma_modes = set()
    grid_variants = set()
    if os.path.isdir(results_dir):
        for fname in os.listdir(results_dir):
            m = re.match(r"(acorn_gamma_\d+)_\d{8}_\d{6}\.json$", fname)
            if m:
                gamma_modes.add(m.group(1))
            gm = re.match(r"(grid_g[\d_]+?)_\d{8}_\d{6}\.json$", fname)
            if gm:
                grid_variants.add(gm.group(1))

    gamma_sorted = sorted(gamma_modes, key=lambda s: int(s.split("_")[-1]))
    grid_sorted = sorted(grid_variants)

    order = []
    for base in BASE_MODE_ORDER:
        order.append(base)
        if base == "acorn":
            order.extend(gamma_sorted)
        if base == "grid":
            order.extend(grid_sorted)

    labels = dict(MODE_LABELS)
    for gm in gamma_sorted:
        gamma_val = gm.split("_")[-1]
        labels[gm] = f"ACORN-\u03b3({gamma_val})"

    grid_tag_labels = {"g1": "Oversample", "g2": "Neighbors", "g3": "SmallCells", "g4": "MaxCells"}
    for gv in grid_sorted:
        tags = gv.replace("grid_", "").split("_")
        tag_desc = "+".join(grid_tag_labels.get(t, t) for t in tags)
        labels[gv] = f"Grid+{tag_desc}"

    return order, labels


def load_latest_results(results_dir, mode_order):
    """Load the most recent result file for each mode."""
    files_by_mode = defaultdict(list)
    if not os.path.isdir(results_dir):
        print(f"Results directory not found: {results_dir}")
        return {}

    modes_longest_first = sorted(mode_order, key=len, reverse=True)

    for fname in os.listdir(results_dir):
        if not fname.endswith(".json"):
            continue
        for mode in modes_longest_first:
            if fname.startswith(f"{mode}_"):
                files_by_mode[mode].append(fname)
                break

    results = {}
    for mode, fnames in files_by_mode.items():
        fnames.sort(reverse=True)
        path = os.path.join(results_dir, fnames[0])
        with open(path) as f:
            results[mode] = json.load(f)
        print(f"  Loaded {mode}: {fnames[0]}")

    return results


def print_comparison(all_results, mode_order, mode_labels):
    available_modes = [m for m in mode_order if m in all_results]
    if not available_modes:
        print("No results found to compare.")
        return

    ref = all_results[available_modes[0]]
    query_types = list(ref.get("results", {}).keys())
    rows = ref.get("rows", "?")
    dim = ref.get("dim", "?")
    k = ref.get("k", "?")

    col_w = 18
    label_w = 20
    sep = "+" + "-" * (label_w + 2)
    for _ in available_modes:
        sep += "+" + "-" * (col_w + 2)
    sep += "+"

    def header_row(title, values):
        line = f"| {title:<{label_w}} "
        for v in values:
            line += f"| {v:>{col_w}} "
        line += "|"
        return line

    print()
    print(f"  Spatial-Vector Benchmark Comparison")
    print(f"  Rows: {rows}  |  Dim: {dim}  |  K: {k}")
    print()

    labels = [mode_labels.get(m, m) for m in available_modes]

    for qt in query_types:
        qt_desc = ref["results"][qt].get("description", qt)
        print(f"  === {qt} ({qt_desc}) ===")
        print(sep)
        print(header_row("Metric", labels))
        print(sep)

        p50s = []
        p95s = []
        p99s = []
        recalls = []

        for m in available_modes:
            data = all_results[m].get("results", {}).get(qt, {})
            rc = all_results[m].get("recalls", {}).get(qt)
            p50s.append(f"{data.get('p50_ms', 0):.1f} ms")
            p95s.append(f"{data.get('p95_ms', 0):.1f} ms")
            p99s.append(f"{data.get('p99_ms', 0):.1f} ms")
            recalls.append(f"{rc:.3f}" if rc is not None else "n/a")

        print(header_row("p50 latency", p50s))
        print(header_row("p95 latency", p95s))
        print(header_row("p99 latency", p99s))
        print(header_row("Recall@K", recalls))

        b0_p50 = all_results.get("b0", {}).get("results", {}).get(qt, {}).get("p50_ms")
        if b0_p50 and b0_p50 > 0:
            speedups = []
            for m in available_modes:
                mp50 = all_results[m].get("results", {}).get(qt, {}).get("p50_ms", 0)
                if mp50 > 0:
                    speedups.append(f"{b0_p50 / mp50:.2f}x")
                else:
                    speedups.append("n/a")
            print(header_row("Speedup vs B0", speedups))

        print(sep)
        print()

    print("  Legend:")
    for m in available_modes:
        desc = all_results[m].get("mode_description", mode_labels.get(m, m))
        print(f"    {mode_labels.get(m, m):20s} -- {desc}")
    print()


def main():
    parser = argparse.ArgumentParser(description="Compare benchmark results")
    parser.add_argument(
        "--results-dir",
        default="results/",
        help="Directory containing result JSON files",
    )
    args = parser.parse_args()

    mode_order, mode_labels = discover_modes(args.results_dir)
    print(f"\nLoading results from {args.results_dir} ...")
    all_results = load_latest_results(args.results_dir, mode_order)

    if not all_results:
        print("No result files found. Run benchmarks first.")
        sys.exit(1)

    print_comparison(all_results, mode_order, mode_labels)


if __name__ == "__main__":
    main()
