#!/usr/bin/env python3
"""
Generate benchmark comparison charts and a full HTML report.

Reads JSON result files from the results/ directory and produces:
  1. Latency bar charts (p50/p95/p99) per query type
  2. Recall bar chart per query type
  3. Speedup vs brute-force chart
  4. A self-contained HTML report with embedded charts and analysis

Usage:
    python3 generate_report.py
    python3 generate_report.py --results-dir path/to/results --output report.html
"""

import argparse
import base64
import io
import json
import os
import sys
from collections import defaultdict
from datetime import datetime

try:
    import matplotlib

    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    import matplotlib.ticker as mticker
    import numpy as np
except ImportError:
    print("ERROR: matplotlib and numpy are required.")
    print("  pip3 install matplotlib numpy")
    sys.exit(1)

BASE_MODE_ORDER = ["b0", "acorn", "grid"]
MODE_LABELS = {
    "b0": "B0: Brute Force",
    "acorn": "ACORN-1",
    "grid": "Grid-HNSW",
}
MODE_COLORS = {
    "b0": "#6c757d",
    "acorn": "#198754",
    "grid": "#dc3545",
}
GAMMA_COLOR_PALETTE = ["#0d6efd", "#6610f2", "#d63384", "#fd7e14", "#20c997"]

QUERY_LABELS = {
    "radius_1km": "Radius 1 km",
    "radius_5km": "Radius 5 km",
    "radius_20km": "Radius 20 km",
    "polygon_downtown": "Polygon\n(downtown)",
    "polygon_metro": "Polygon\n(metro)",
}


GRID_COLOR_PALETTE = ["#e63946", "#f4a261", "#2a9d8f", "#264653", "#e9c46a"]


def discover_modes(results_dir):
    """Scan the results directory and return the full MODE_ORDER including any
    acorn_gamma_N and grid_gN variants found, along with updated labels and colors."""
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
    colors = dict(MODE_COLORS)
    for i, gm in enumerate(gamma_sorted):
        gamma_val = gm.split("_")[-1]
        labels[gm] = f"ACORN-\u03b3({gamma_val})"
        colors[gm] = GAMMA_COLOR_PALETTE[i % len(GAMMA_COLOR_PALETTE)]

    grid_tag_labels = {"g1": "Oversample", "g2": "Neighbors", "g3": "SmallCells", "g4": "MaxCells"}
    for i, gv in enumerate(grid_sorted):
        tags = gv.replace("grid_", "").split("_")
        tag_desc = "+".join(grid_tag_labels.get(t, t) for t in tags)
        labels[gv] = f"Grid+{tag_desc}"
        colors[gv] = GRID_COLOR_PALETTE[i % len(GRID_COLOR_PALETTE)]

    return order, labels, colors


def load_latest_results(results_dir, mode_order):
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


def fig_to_base64(fig):
    buf = io.BytesIO()
    fig.savefig(buf, format="png", dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)
    buf.seek(0)
    return base64.b64encode(buf.read()).decode("utf-8")


def fig_to_file(fig, path):
    fig.savefig(path, format="png", dpi=150, bbox_inches="tight", facecolor="white")
    plt.close(fig)


def make_latency_chart(all_results, query_types, available_modes, percentile="p50",
                       mode_labels=None, mode_colors=None):
    mode_labels = mode_labels or MODE_LABELS
    mode_colors = mode_colors or MODE_COLORS
    label_map = {"p50": "p50 (Median)", "p95": "p95", "p99": "p99"}
    fig, ax = plt.subplots(figsize=(12, 5))

    n_qt = len(query_types)
    n_modes = len(available_modes)
    width = 0.7 / n_modes
    x = np.arange(n_qt)

    for i, mode in enumerate(available_modes):
        vals = []
        for qt in query_types:
            data = all_results[mode].get("results", {}).get(qt, {})
            vals.append(data.get(f"{percentile}_ms", 0))
        offset = (i - (n_modes - 1) / 2) * width
        bars = ax.bar(
            x + offset,
            vals,
            width,
            label=mode_labels.get(mode, mode),
            color=mode_colors.get(mode, "#999"),
            edgecolor="white",
            linewidth=0.5,
        )
        for bar, v in zip(bars, vals):
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 0.5,
                f"{v:.1f}",
                ha="center",
                va="bottom",
                fontsize=7,
                fontweight="bold",
            )

    ax.set_ylabel("Latency (ms)", fontsize=11)
    ax.set_title(
        f"{label_map.get(percentile, percentile)} Latency by Query Type",
        fontsize=13,
        fontweight="bold",
    )
    ax.set_xticks(x)
    ax.set_xticklabels([QUERY_LABELS.get(qt, qt) for qt in query_types], fontsize=9)
    ax.legend(fontsize=9)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    fig.tight_layout()
    return fig


def make_recall_chart(all_results, query_types, available_modes,
                      mode_labels=None, mode_colors=None):
    mode_labels = mode_labels or MODE_LABELS
    mode_colors = mode_colors or MODE_COLORS
    fig, ax = plt.subplots(figsize=(12, 5))

    n_qt = len(query_types)
    n_modes = len(available_modes)
    width = 0.7 / n_modes
    x = np.arange(n_qt)

    for i, mode in enumerate(available_modes):
        vals = []
        for qt in query_types:
            rc = all_results[mode].get("recalls", {}).get(qt)
            vals.append(rc if rc is not None else 0)
        offset = (i - (n_modes - 1) / 2) * width
        bars = ax.bar(
            x + offset,
            vals,
            width,
            label=mode_labels.get(mode, mode),
            color=mode_colors.get(mode, "#999"),
            edgecolor="white",
            linewidth=0.5,
        )
        for bar, v in zip(bars, vals):
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 0.005,
                f"{v:.3f}",
                ha="center",
                va="bottom",
                fontsize=7,
                fontweight="bold",
            )

    ax.set_ylabel("Recall@K", fontsize=11)
    ax.set_title("Recall@K by Query Type", fontsize=13, fontweight="bold")
    ax.set_xticks(x)
    ax.set_xticklabels([QUERY_LABELS.get(qt, qt) for qt in query_types], fontsize=9)
    ax.set_ylim(0, 1.1)
    ax.legend(fontsize=9)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    fig.tight_layout()
    return fig


def make_speedup_chart(all_results, query_types, available_modes,
                       mode_labels=None, mode_colors=None):
    mode_labels = mode_labels or MODE_LABELS
    mode_colors = mode_colors or MODE_COLORS
    fig, ax = plt.subplots(figsize=(12, 5))

    n_qt = len(query_types)
    n_modes = len(available_modes)
    width = 0.7 / n_modes
    x = np.arange(n_qt)

    for i, mode in enumerate(available_modes):
        vals = []
        for qt in query_types:
            b0_p50 = all_results.get("b0", {}).get("results", {}).get(qt, {}).get("p50_ms", 0)
            mode_p50 = all_results[mode].get("results", {}).get(qt, {}).get("p50_ms", 0)
            if mode_p50 > 0 and b0_p50 > 0:
                vals.append(b0_p50 / mode_p50)
            else:
                vals.append(0)
        offset = (i - (n_modes - 1) / 2) * width
        bars = ax.bar(
            x + offset,
            vals,
            width,
            label=mode_labels.get(mode, mode),
            color=mode_colors.get(mode, "#999"),
            edgecolor="white",
            linewidth=0.5,
        )
        for bar, v in zip(bars, vals):
            ax.text(
                bar.get_x() + bar.get_width() / 2,
                bar.get_height() + 0.02,
                f"{v:.2f}x",
                ha="center",
                va="bottom",
                fontsize=7,
                fontweight="bold",
            )

    ax.set_ylabel("Speedup vs Brute Force", fontsize=11)
    ax.set_title(
        "Speedup vs B0 (Brute Force) -- p50 Latency",
        fontsize=13,
        fontweight="bold",
    )
    ax.set_xticks(x)
    ax.set_xticklabels([QUERY_LABELS.get(qt, qt) for qt in query_types], fontsize=9)
    ax.axhline(y=1.0, color="red", linestyle="--", alpha=0.5, linewidth=1)
    ax.legend(fontsize=9)
    ax.grid(axis="y", alpha=0.3)
    ax.set_axisbelow(True)
    fig.tight_layout()
    return fig


def make_latency_distribution_chart(all_results, query_types, available_modes,
                                    mode_labels=None, mode_colors=None):
    mode_labels = mode_labels or MODE_LABELS
    mode_colors = mode_colors or MODE_COLORS
    fig, axes = plt.subplots(1, len(query_types), figsize=(4 * len(query_types), 4), sharey=False)
    if len(query_types) == 1:
        axes = [axes]

    for idx, qt in enumerate(query_types):
        ax = axes[idx]
        data_to_plot = []
        labels = []
        colors = []
        for mode in available_modes:
            lats = all_results[mode].get("results", {}).get(qt, {}).get("latencies_ms", [])
            if lats:
                data_to_plot.append(lats)
                labels.append(mode_labels.get(mode, mode))
                colors.append(mode_colors.get(mode, "#999"))
        if data_to_plot:
            bp = ax.boxplot(data_to_plot, labels=labels, patch_artist=True, widths=0.6)
            for patch, color in zip(bp["boxes"], colors):
                patch.set_facecolor(color)
                patch.set_alpha(0.7)
        ax.set_title(QUERY_LABELS.get(qt, qt), fontsize=10, fontweight="bold")
        ax.set_ylabel("Latency (ms)" if idx == 0 else "", fontsize=9)
        ax.grid(axis="y", alpha=0.3)
        ax.set_axisbelow(True)
        ax.tick_params(axis="x", rotation=45, labelsize=7)

    fig.suptitle("Latency Distribution (Box Plot)", fontsize=13, fontweight="bold", y=1.02)
    fig.tight_layout()
    return fig


def build_data_table(all_results, query_types, available_modes):
    rows = []
    for qt in query_types:
        row = {"query_type": qt, "label": QUERY_LABELS.get(qt, qt).replace("\n", " ")}
        for mode in available_modes:
            data = all_results[mode].get("results", {}).get(qt, {})
            rc = all_results[mode].get("recalls", {}).get(qt)
            b0_p50 = all_results.get("b0", {}).get("results", {}).get(qt, {}).get("p50_ms", 0)
            mode_p50 = data.get("p50_ms", 0)
            speedup = b0_p50 / mode_p50 if mode_p50 > 0 and b0_p50 > 0 else 0
            row[mode] = {
                "p50": data.get("p50_ms", 0),
                "p95": data.get("p95_ms", 0),
                "p99": data.get("p99_ms", 0),
                "qps": data.get("qps", 0),
                "recall": rc,
                "speedup": speedup,
            }
        rows.append(row)
    return rows


def generate_analysis(data_table, available_modes, mode_labels=None):
    mode_labels = mode_labels or MODE_LABELS
    sections = []

    sections.append(
        "<h2>Why These Results?</h2>"
        "<p>The methods represent fundamentally different strategies for "
        "answering <em>spatial + vector</em> queries (\"find the K nearest embeddings "
        "within a geographic region\"):</p>"
        "<ul>"
        "<li><strong>B0 (Brute Force)</strong> scans every row, computes distances, "
        "applies the spatial filter, and sorts. It is 100% accurate (recall = 1.0) but "
        "O(N) in both vector distance and spatial predicate evaluation.</li>"
        "<li><strong>ACORN-1</strong> is a predicate-aware HNSW variant. During graph "
        "traversal it expands 2-hop neighbors and evaluates the spatial predicate inline, "
        "allowing it to leverage the HNSW graph structure while filtering. This produces "
        "sub-linear search time with high recall, though recall may drop below 1.0 "
        "for very selective predicates where the graph neighbourhood is sparse.</li>"
        "<li><strong>ACORN-&gamma;</strong> builds a denser HNSW graph (M &times; &gamma; "
        "neighbors) while searching with the base-M early-stop threshold. Higher &gamma; "
        "provides more routing options through the graph at the cost of larger indexes.</li>"
        "<li><strong>Grid-HNSW</strong> partitions vectors by S2 cell at write time and "
        "builds a per-cell HNSW index. At query time, only cells overlapping the spatial "
        "predicate are searched, reducing the search space proportionally to the "
        "geographic selectivity of the query.</li>"
        "</ul>"
    )

    def _mode_summary(mode, label):
        recalls = [
            r[mode]["recall"] for r in data_table
            if r.get(mode, {}).get("recall") is not None
        ]
        avg_recall = sum(recalls) / len(recalls) if recalls else 0
        speedups = [r[mode]["speedup"] for r in data_table if mode in r]
        avg_speedup = sum(speedups) / len(speedups) if speedups else 0

        sections.append(
            f"<h2>{label} Performance Summary</h2>"
            "<ul>"
            f"<li><strong>Average Recall@K:</strong> {avg_recall:.3f}</li>"
            f"<li><strong>Average Speedup vs Brute Force:</strong> {avg_speedup:.2f}x</li>"
            "</ul>"
        )

        if avg_recall > 0 and avg_recall < 0.90:
            sections.append(
                f"<p>{label} recall is below 90%. Consider increasing "
                "<code>ef_search</code> or using a higher gamma for denser graph connectivity.</p>"
            )
        elif avg_recall >= 0.95:
            sections.append(
                f"<p>{label} achieves strong recall (&gt;95%), indicating effective "
                "graph navigation under spatial constraints.</p>"
            )

    for mode in available_modes:
        if mode == "b0":
            continue
        label = mode_labels.get(mode, mode)
        if mode in [r_key for row in data_table for r_key in row if r_key not in ("query_type", "label")]:
            _mode_summary(mode, label)

    gamma_modes = [m for m in available_modes if m.startswith("acorn_gamma_")]
    if len(gamma_modes) > 1:
        sections.append(
            "<h2>Gamma Comparison</h2>"
            "<p>Multiple &gamma; values were tested. Compare recall and latency across "
            "gamma variants above to identify the optimal &gamma; for your workload. "
            "Higher &gamma; typically improves recall at the cost of index size and "
            "build time.</p>"
        )

    sections.append(
        "<h2>Selectivity Impact</h2>"
        "<p>Query types vary in spatial selectivity:</p>"
        "<ul>"
        "<li><strong>radius_1km</strong> is very selective (few qualifying rows), "
        "making it hardest for ANN methods since most graph neighbors are filtered out.</li>"
        "<li><strong>radius_20km / polygon_metro</strong> are broad filters that qualify "
        "many rows, allowing the ANN graph to navigate more freely.</li>"
        "</ul>"
        "<p>We expect ACORN methods to perform best on moderate-to-broad selectivity and "
        "potentially struggle on very tight spatial filters.</p>"
    )

    return "\n".join(sections)


def _recall_color(recall):
    """Return a CSS background color for a recall value (0.0-1.0)."""
    if recall is None:
        return "#f8f9fa"
    r = max(0.0, min(1.0, recall))
    if r >= 0.95:
        return "#d4edda"
    elif r >= 0.80:
        return "#d1ecf1"
    elif r >= 0.50:
        return "#fff3cd"
    elif r >= 0.20:
        return "#fde2c8"
    else:
        return "#f8d7da"


def _recall_class(recall):
    if recall is None:
        return "rc-na"
    if recall >= 0.95:
        return "rc-great"
    elif recall >= 0.80:
        return "rc-good"
    elif recall >= 0.50:
        return "rc-fair"
    elif recall >= 0.20:
        return "rc-poor"
    return "rc-bad"


def _build_recall_heatmap(data_table, available_modes, mode_labels):
    """Recall-focused heatmap table: modes as rows, query types as columns."""
    query_types = [row["query_type"] for row in data_table]
    qt_labels = [row["label"] for row in data_table]

    html = '<table class="heatmap-table"><thead><tr><th class="sticky-col">Strategy</th>'
    for label in qt_labels:
        html += f"<th>{label}</th>"
    html += "<th>Average</th></tr></thead><tbody>"

    for mode in available_modes:
        label = mode_labels.get(mode, mode)
        html += f'<tr><td class="sticky-col mode-label">{label}</td>'
        recalls = []
        for row in data_table:
            d = row.get(mode, {})
            rc = d.get("recall")
            recalls.append(rc)
            if rc is not None:
                cls = _recall_class(rc)
                html += f'<td class="{cls}">{rc:.3f}</td>'
            else:
                html += '<td class="rc-na">n/a</td>'
        valid = [r for r in recalls if r is not None]
        avg = sum(valid) / len(valid) if valid else None
        if avg is not None:
            cls = _recall_class(avg)
            html += f'<td class="{cls} avg-cell">{avg:.3f}</td>'
        else:
            html += '<td class="rc-na avg-cell">n/a</td>'
        html += "</tr>"
    html += "</tbody></table>"
    return html


def _build_detail_tables(data_table, available_modes, mode_labels):
    """Per-query-type detail cards: one compact table per query type."""
    cards = []
    for row in data_table:
        qt = row["query_type"]
        label = row["label"]

        best_p50 = None
        best_recall = None
        for mode in available_modes:
            d = row.get(mode, {})
            p50 = d.get("p50", 0)
            rc = d.get("recall")
            if mode != "b0" and p50 > 0:
                if best_p50 is None or p50 < best_p50:
                    best_p50 = p50
            if rc is not None and mode != "b0":
                if best_recall is None or rc > best_recall:
                    best_recall = rc

        card = f'<div class="detail-card"><h3>{label}</h3>'
        card += '<table class="detail-table"><thead><tr>'
        card += "<th>Strategy</th><th>p50 ms</th><th>p95 ms</th><th>p99 ms</th>"
        card += "<th>Recall</th><th>Speedup</th></tr></thead><tbody>"

        for mode in available_modes:
            d = row.get(mode, {})
            p50 = d.get("p50", 0)
            p95 = d.get("p95", 0)
            p99 = d.get("p99", 0)
            rc = d.get("recall")
            sp = d.get("speedup", 0)
            ml = mode_labels.get(mode, mode)

            p50_cls = ' class="best-val"' if p50 == best_p50 and mode != "b0" else ""
            rc_cls = _recall_class(rc) if rc is not None else "rc-na"
            is_best_rc = rc == best_recall and mode != "b0" and rc is not None
            rc_extra = " best-val" if is_best_rc else ""

            rc_str = f"{rc:.3f}" if rc is not None else "n/a"
            sp_str = f"{sp:.2f}x" if sp else "1.00x"

            card += f"<tr>"
            card += f'<td class="mode-label">{ml}</td>'
            card += f"<td{p50_cls}>{p50:.1f}</td>"
            card += f"<td>{p95:.1f}</td>"
            card += f"<td>{p99:.1f}</td>"
            card += f'<td class="{rc_cls}{rc_extra}">{rc_str}</td>'
            card += f"<td>{sp_str}</td>"
            card += "</tr>"

        card += "</tbody></table></div>"
        cards.append(card)

    return "\n".join(cards)


def _build_summary_cards(data_table, available_modes, mode_labels, mode_colors):
    """Top-level summary cards: one per non-B0 mode with avg recall and speedup."""
    cards = []
    for mode in available_modes:
        if mode == "b0":
            continue
        label = mode_labels.get(mode, mode)
        color = mode_colors.get(mode, "#6c757d")
        recalls = [
            r[mode]["recall"]
            for r in data_table
            if r.get(mode, {}).get("recall") is not None
        ]
        speedups = [r[mode]["speedup"] for r in data_table if mode in r]
        avg_recall = sum(recalls) / len(recalls) if recalls else 0
        avg_speedup = sum(speedups) / len(speedups) if speedups else 0
        rc_cls = _recall_class(avg_recall)
        cards.append(
            f'<div class="summary-card" style="border-top: 4px solid {color}">'
            f'<div class="sc-name">{label}</div>'
            f'<div class="sc-metrics">'
            f'<div class="sc-metric"><span class="sc-label">Avg Recall</span>'
            f'<span class="sc-value {rc_cls}">{avg_recall:.3f}</span></div>'
            f'<div class="sc-metric"><span class="sc-label">Avg Speedup</span>'
            f'<span class="sc-value">{avg_speedup:.2f}x</span></div>'
            f'</div></div>'
        )
    return "\n".join(cards)


def generate_html_report(all_results, charts_b64, data_table, available_modes, output_path,
                         mode_labels=None, mode_colors=None):
    mode_labels = mode_labels or MODE_LABELS
    mode_colors = mode_colors or MODE_COLORS
    ref = all_results[available_modes[0]]
    rows = ref.get("rows", "?")
    dim = ref.get("dim", "?")
    k = ref.get("k", "?")
    timestamp = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    analysis = generate_analysis(data_table, available_modes, mode_labels)
    recall_heatmap = _build_recall_heatmap(data_table, available_modes, mode_labels)
    detail_tables = _build_detail_tables(data_table, available_modes, mode_labels)
    summary_cards = _build_summary_cards(data_table, available_modes, mode_labels, mode_colors)

    html = f"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>Spatial-Vector Benchmark Report</title>
<style>
  * {{ margin: 0; padding: 0; box-sizing: border-box; }}
  body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
         line-height: 1.6; color: #1e293b; background: #f1f5f9; padding: 2rem; }}
  .container {{ max-width: 1400px; margin: 0 auto; }}
  h1 {{ font-size: 1.75rem; color: #0f172a; margin-bottom: 0.25rem; font-weight: 700; }}
  .subtitle {{ color: #64748b; margin-bottom: 1.5rem; font-size: 0.875rem; }}
  h2 {{ font-size: 1.25rem; color: #1e293b; margin: 2.5rem 0 1rem;
        padding-bottom: 0.5rem; border-bottom: 2px solid #e2e8f0; font-weight: 600; }}
  h3 {{ font-size: 1rem; color: #334155; margin: 0 0 0.75rem; font-weight: 600; }}
  p, li {{ margin-bottom: 0.5rem; font-size: 0.9rem; }}
  ul {{ padding-left: 1.5rem; }}
  code {{ background: #e2e8f0; padding: 0.125rem 0.375rem; border-radius: 3px;
          font-size: 0.8rem; font-family: 'SF Mono', Monaco, monospace; }}

  .meta {{ display: flex; gap: 1.5rem; flex-wrap: wrap; background: white;
           padding: 1rem 1.25rem; border-radius: 10px; margin-bottom: 1.5rem;
           box-shadow: 0 1px 3px rgba(0,0,0,0.06); border: 1px solid #e2e8f0; }}
  .meta-item {{ font-size: 0.85rem; }}
  .meta-item strong {{ color: #0f766e; }}

  .summary-grid {{ display: grid; grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
                    gap: 1rem; margin: 1.5rem 0; }}
  .summary-card {{ background: white; border-radius: 10px; padding: 1rem 1.25rem;
                   box-shadow: 0 1px 3px rgba(0,0,0,0.06); border: 1px solid #e2e8f0; }}
  .sc-name {{ font-weight: 600; font-size: 0.85rem; color: #334155; margin-bottom: 0.75rem; }}
  .sc-metrics {{ display: flex; gap: 1.5rem; }}
  .sc-metric {{ display: flex; flex-direction: column; }}
  .sc-label {{ font-size: 0.7rem; color: #94a3b8; text-transform: uppercase;
               letter-spacing: 0.05em; font-weight: 500; }}
  .sc-value {{ font-size: 1.25rem; font-weight: 700; color: #1e293b; }}

  .heatmap-table {{ width: 100%; border-collapse: separate; border-spacing: 0;
                    background: white; border-radius: 10px; overflow: hidden;
                    box-shadow: 0 1px 3px rgba(0,0,0,0.06); border: 1px solid #e2e8f0; }}
  .heatmap-table th {{ background: #1e293b; color: #f8fafc; padding: 0.625rem 0.75rem;
                       font-size: 0.75rem; text-transform: uppercase; letter-spacing: 0.05em;
                       font-weight: 600; text-align: center; white-space: nowrap; }}
  .heatmap-table td {{ padding: 0.5rem 0.75rem; text-align: center; font-size: 0.85rem;
                       font-weight: 600; border-bottom: 1px solid #f1f5f9;
                       font-variant-numeric: tabular-nums; }}
  .heatmap-table tr:last-child td {{ border-bottom: none; }}
  .heatmap-table tr:hover td {{ filter: brightness(0.97); }}

  .rc-great {{ background: #dcfce7; color: #166534; }}
  .rc-good {{ background: #dbeafe; color: #1e40af; }}
  .rc-fair {{ background: #fef9c3; color: #854d0e; }}
  .rc-poor {{ background: #fed7aa; color: #9a3412; }}
  .rc-bad {{ background: #fecaca; color: #991b1b; }}
  .rc-na {{ background: #f8fafc; color: #94a3b8; }}

  .mode-label {{ text-align: left !important; font-weight: 600; color: #334155;
                 white-space: nowrap; }}
  .avg-cell {{ font-weight: 700 !important; border-left: 2px solid #cbd5e1; }}
  .best-val {{ font-weight: 800; text-decoration: underline; text-decoration-thickness: 2px;
               text-underline-offset: 2px; }}
  .sticky-col {{ position: sticky; left: 0; background: inherit; z-index: 1; }}
  .heatmap-table th.sticky-col {{ background: #1e293b; z-index: 2; }}

  .detail-grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(500px, 1fr));
                   gap: 1.25rem; margin: 1.5rem 0; }}
  .detail-card {{ background: white; border-radius: 10px; padding: 1.25rem;
                  box-shadow: 0 1px 3px rgba(0,0,0,0.06); border: 1px solid #e2e8f0; }}
  .detail-table {{ width: 100%; border-collapse: collapse; }}
  .detail-table th {{ background: #f8fafc; color: #475569; padding: 0.5rem 0.625rem;
                      font-size: 0.7rem; text-transform: uppercase; letter-spacing: 0.05em;
                      font-weight: 600; text-align: center; border-bottom: 2px solid #e2e8f0; }}
  .detail-table td {{ padding: 0.4rem 0.625rem; text-align: center; font-size: 0.8rem;
                      border-bottom: 1px solid #f1f5f9;
                      font-variant-numeric: tabular-nums; }}
  .detail-table tr:last-child td {{ border-bottom: none; }}
  .detail-table tr:hover td {{ background: #f8fafc; }}

  .chart {{ background: white; border-radius: 10px; padding: 1.25rem;
            margin: 1.25rem 0; box-shadow: 0 1px 3px rgba(0,0,0,0.06);
            border: 1px solid #e2e8f0; }}
  .chart img {{ width: 100%; height: auto; display: block; }}

  .footer {{ margin-top: 3rem; padding-top: 1rem; border-top: 1px solid #e2e8f0;
             color: #94a3b8; font-size: 0.75rem; text-align: center; }}

  .legend {{ display: flex; gap: 0.75rem; flex-wrap: wrap; margin: 0.75rem 0; }}
  .legend-item {{ display: flex; align-items: center; gap: 0.375rem; font-size: 0.75rem;
                  color: #64748b; }}
  .legend-swatch {{ width: 14px; height: 14px; border-radius: 3px; border: 1px solid rgba(0,0,0,0.1); }}

  @media (max-width: 768px) {{
    body {{ padding: 1rem; }}
    .detail-grid {{ grid-template-columns: 1fr; }}
    .summary-grid {{ grid-template-columns: 1fr 1fr; }}
    .heatmap-table {{ font-size: 0.75rem; }}
  }}
</style>
</head>
<body>
<div class="container">
  <h1>Spatial-Vector Benchmark Report</h1>
  <p class="subtitle">Generated {timestamp}</p>

  <div class="meta">
    <div class="meta-item"><strong>Rows:</strong> {rows:,}</div>
    <div class="meta-item"><strong>Dimensions:</strong> {dim}</div>
    <div class="meta-item"><strong>Top-K:</strong> {k}</div>
    <div class="meta-item"><strong>Modes:</strong> {', '.join(mode_labels.get(m, m) for m in available_modes)}</div>
  </div>

  <h2>Strategy Performance Summary</h2>
  <div class="summary-grid">
    {summary_cards}
  </div>

  <h2>Recall Heatmap</h2>
  <div class="legend">
    <div class="legend-item"><div class="legend-swatch" style="background:#dcfce7"></div> &ge; 0.95</div>
    <div class="legend-item"><div class="legend-swatch" style="background:#dbeafe"></div> 0.80 &ndash; 0.94</div>
    <div class="legend-item"><div class="legend-swatch" style="background:#fef9c3"></div> 0.50 &ndash; 0.79</div>
    <div class="legend-item"><div class="legend-swatch" style="background:#fed7aa"></div> 0.20 &ndash; 0.49</div>
    <div class="legend-item"><div class="legend-swatch" style="background:#fecaca"></div> &lt; 0.20</div>
  </div>
  {recall_heatmap}

  <h2>Detailed Metrics by Query Type</h2>
  <div class="detail-grid">
    {detail_tables}
  </div>

  <h2>Latency Charts</h2>
  <div class="chart"><img src="data:image/png;base64,{charts_b64['p50']}" alt="p50 latency"></div>
  <div class="chart"><img src="data:image/png;base64,{charts_b64['p95']}" alt="p95 latency"></div>
  <div class="chart"><img src="data:image/png;base64,{charts_b64['p99']}" alt="p99 latency"></div>

  <h2>Recall Comparison</h2>
  <div class="chart"><img src="data:image/png;base64,{charts_b64['recall']}" alt="Recall"></div>

  <h2>Speedup vs Brute Force</h2>
  <div class="chart"><img src="data:image/png;base64,{charts_b64['speedup']}" alt="Speedup"></div>

  <h2>Latency Distribution</h2>
  <div class="chart"><img src="data:image/png;base64,{charts_b64['boxplot']}" alt="Distribution"></div>

  {analysis}

  <div class="footer">
    Generated by <code>generate_report.py</code> &mdash; StarRocks Spatial-Vector Benchmark Suite
  </div>
</div>
</body>
</html>"""

    with open(output_path, "w") as f:
        f.write(html)
    print(f"  Report written to {output_path}")


def main():
    parser = argparse.ArgumentParser(description="Generate benchmark comparison report")
    parser.add_argument(
        "--results-dir",
        default="results/",
        help="Directory containing result JSON files",
    )
    parser.add_argument(
        "--output",
        default=None,
        help="Output HTML report path (default: results/benchmark_report.html)",
    )
    parser.add_argument(
        "--save-charts",
        action="store_true",
        help="Also save individual chart PNGs to the results directory",
    )
    args = parser.parse_args()

    if args.output is None:
        args.output = os.path.join(args.results_dir, "benchmark_report.html")

    print(f"\nDiscovering modes from {args.results_dir} ...")
    mode_order, mode_labels, mode_colors = discover_modes(args.results_dir)
    print(f"  Mode order: {mode_order}")

    print(f"\nLoading results from {args.results_dir} ...")
    all_results = load_latest_results(args.results_dir, mode_order)

    if not all_results:
        print("No result files found. Run benchmarks first.")
        sys.exit(1)

    available_modes = [m for m in mode_order if m in all_results]
    ref = all_results[available_modes[0]]
    query_types = list(ref.get("results", {}).keys())

    print(f"  Modes: {available_modes}")
    print(f"  Query types: {query_types}")

    print("\nGenerating charts...")
    charts_b64 = {}

    fig = make_latency_chart(all_results, query_types, available_modes, "p50", mode_labels, mode_colors)
    charts_b64["p50"] = fig_to_base64(fig)

    fig = make_latency_chart(all_results, query_types, available_modes, "p95", mode_labels, mode_colors)
    charts_b64["p95"] = fig_to_base64(fig)

    fig = make_latency_chart(all_results, query_types, available_modes, "p99", mode_labels, mode_colors)
    charts_b64["p99"] = fig_to_base64(fig)

    fig = make_recall_chart(all_results, query_types, available_modes, mode_labels, mode_colors)
    charts_b64["recall"] = fig_to_base64(fig)

    fig = make_speedup_chart(all_results, query_types, available_modes, mode_labels, mode_colors)
    charts_b64["speedup"] = fig_to_base64(fig)

    fig = make_latency_distribution_chart(all_results, query_types, available_modes, mode_labels, mode_colors)
    charts_b64["boxplot"] = fig_to_base64(fig)

    if args.save_charts:
        for name in ["p50", "p95", "p99", "recall", "speedup", "boxplot"]:
            chart_path = os.path.join(args.results_dir, f"chart_{name}.png")
            print(f"  Saved {chart_path}")

    print("\nBuilding data table...")
    data_table = build_data_table(all_results, query_types, available_modes)

    print("Generating HTML report...")
    generate_html_report(all_results, charts_b64, data_table, available_modes, args.output,
                         mode_labels, mode_colors)

    print(f"\nDone! Open the report in a browser:")
    print(f"  open {args.output}")
    print()


if __name__ == "__main__":
    main()
