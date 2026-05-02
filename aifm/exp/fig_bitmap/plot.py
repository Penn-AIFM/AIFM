#!/usr/bin/env python3
"""Plot fig_bitmap results directly from log files."""

import argparse
import re
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

MOPS_RE = re.compile(r"mops\s*=\s*([0-9]*\.?[0-9]+)")
HIT_RE = re.compile(r"hit_rate\s*=\s*([0-9]*\.?[0-9]+)")


def parse_skew_from_name(path: Path):
    # Expected format: log.<zipf_s>, e.g., log.1.35
    suffix = path.name.split("log.", 1)[1]
    return float(suffix)


def parse_log(path: Path):
    text = path.read_text()
    mops_match = MOPS_RE.search(text)
    hit_match = HIT_RE.search(text)
    if not mops_match:
        return None
    return {
        "skew": parse_skew_from_name(path),
        "mops": float(mops_match.group(1)),
        "hit_rate": float(hit_match.group(1)) if hit_match else None,
        "file": path.name,
    }


def load_series(log_dir: Path):
    rows = []
    for log in sorted(log_dir.glob("log.*")):
        try:
            row = parse_log(log)
        except (IndexError, ValueError):
            continue
        if row is not None:
            rows.append(row)
    rows.sort(key=lambda x: x["skew"])
    return rows


def main():
    base = Path(__file__).resolve().parent
    ap = argparse.ArgumentParser(description="Plot fig_bitmap logs.")
    ap.add_argument("--aifm-dir", type=Path, default=base / "aifm")
    ap.add_argument("--linux-dir", type=Path, default=base / "linux_mem")
    ap.add_argument("--output", type=Path, default=base / "bitmap_mops.png")
    ap.add_argument(
        "--include-hit-rate",
        action="store_true",
        help="Also plot hit_rate in a second subplot.",
    )
    args = ap.parse_args()

    aifm = load_series(args.aifm_dir)
    linux = load_series(args.linux_dir)
    if not aifm and not linux:
        raise SystemExit("No parseable logs found in either directory.")

    if args.include_hit_rate:
        fig, (ax0, ax1) = plt.subplots(2, 1, figsize=(8, 8), sharex=True)
    else:
        fig, ax0 = plt.subplots(1, 1, figsize=(8, 5))
        ax1 = None

    for label, rows, style in (
        ("AIFM", aifm, "o-"),
        ("All local mem", linux, "s-"),
    ):
        if not rows:
            continue
        xs = [r["skew"] for r in rows]
        ys = [r["mops"] for r in rows]
        ax0.plot(xs, ys, style, label=label, markersize=5)
        if ax1 is not None:
            hrs = [r["hit_rate"] if r["hit_rate"] is not None else float("nan") for r in rows]
            ax1.plot(xs, hrs, style, label=label, markersize=5)

    ax0.set_ylabel("Throughput (MOPS)")
    ax0.set_title("fig_bitmap: throughput vs Zipf skew")
    ax0.grid(True, linestyle=":", alpha=0.6)
    ax0.legend(loc="best")

    if ax1 is not None:
        ax1.set_xlabel("Zipf skew parameter s")
        ax1.set_ylabel("Bit=1 rate")
        ax1.grid(True, linestyle=":", alpha=0.6)
    else:
        ax0.set_xlabel("Zipf skew parameter s")

    fig.tight_layout()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(str(args.output), dpi=150)
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
