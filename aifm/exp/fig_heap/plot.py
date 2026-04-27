#!/usr/bin/env python3
"""Plot heap experiment results from summary (see parse.sh)."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def load_summary(path: Path):
    rows = {"aifm": [], "local": []}
    with path.open(newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            backend = row["backend"].strip()
            if backend not in rows:
                continue
            rows[backend].append(
                {
                    "N": int(row["N"]),
                    "build_us": int(row["build_us"]),
                    "drain_us": int(row["drain_us"]),
                    "total_us": int(row["total_us"]),
                }
            )
    for k in rows:
        rows[k].sort(key=lambda x: x["N"])
    return rows


def main():
    ap = argparse.ArgumentParser(description="Plot fig_heap summary CSV.")
    ap.add_argument(
        "-i",
        "--input",
        type=Path,
        default=Path(__file__).resolve().parent / "summary",
        help="Path to summary CSV (default: next to this script)",
    )
    ap.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "heap_total_us.png",
        help="Output PNG path",
    )
    args = ap.parse_args()

    data = load_summary(args.input)
    fig, ax = plt.subplots(figsize=(8, 5))

    for label, key, style in (
        ("AIFM heap (TCP)", "aifm", "o-"),
        ("Local std::push_heap / pop_heap", "local", "s-"),
    ):
        pts = data.get(key) or []
        if not pts:
            continue
        xs = [p["N"] for p in pts]
        ys = [p["total_us"] for p in pts]
        ax.plot(xs, ys, style, label=label, markersize=6)

    ax.set_xlabel("Heap size N")
    ax.set_ylabel("total_us (microseconds)")
    ax.set_title("Heap build+drain time vs N")
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.grid(True, which="both", linestyle=":", alpha=0.6)
    ax.legend(loc="best")
    fig.tight_layout()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=150)
    print(f"Wrote {args.output}")
    plt.close()


if __name__ == "__main__":
    main()
