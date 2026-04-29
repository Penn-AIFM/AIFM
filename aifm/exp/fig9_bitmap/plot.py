#!/usr/bin/env python3
"""Plot fig9_bitmap summary CSV (see parse.sh)."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def load_summary(path: Path):
    rows = {"aifm": [], "linux_mem": []}
    with path.open(newline="") as f:
        r = csv.DictReader(f)
        for row in r:
            backend = row["backend"].strip()
            if backend not in rows:
                continue
            rows[backend].append(
                {
                    "zipf_s": float(row["zipf_s"]),
                    "mops": float(row["mops"]),
                }
            )
    for key in rows:
        rows[key].sort(key=lambda x: x["zipf_s"])
    return rows


def main():
    ap = argparse.ArgumentParser(description="Plot fig9_bitmap summary CSV.")
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
        default=Path(__file__).resolve().parent / "bitmap_zipf_mops.png",
        help="Output PNG path",
    )
    args = ap.parse_args()

    data = load_summary(args.input)
    fig, ax = plt.subplots(figsize=(8, 5))

    for label, key, style in (
        ("AIFM bitmap (TCP)", "aifm", "o-"),
        ("Local bitmap (FakeDevice)", "linux_mem", "s-"),
    ):
        pts = data.get(key) or []
        if not pts:
            continue
        xs = [p["zipf_s"] for p in pts]
        ys = [p["mops"] for p in pts]
        ax.plot(xs, ys, style, label=label, markersize=5)

    ax.set_xlabel("Zipf skew parameter s")
    ax.set_ylabel("Throughput (MOPS)")
    ax.set_title("Bitmap lookup throughput vs skew")
    ax.grid(True, linestyle=":", alpha=0.6)
    ax.legend(loc="best")
    fig.tight_layout()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(args.output, dpi=150)
    print(f"Wrote {args.output}")
    plt.close()


if __name__ == "__main__":
    main()
