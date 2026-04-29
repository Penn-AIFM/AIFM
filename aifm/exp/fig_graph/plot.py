#!/usr/bin/env python3
"""Plot graph experiment summary.csv produced by parse.sh."""

import argparse
import csv
from pathlib import Path

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def load_rows(path: Path):
    out = {}
    with path.open(newline="") as f:
        reader = csv.DictReader(f)
        for r in reader:
            b = r["backend"].strip()
            out.setdefault(b, []).append(
                {
                    "N": int(r["N"]),
                    "local_bfs_us": float(r["local_bfs_us"]),
                    "aifm_no_prefetch_us": float(r["aifm_no_prefetch_us"]),
                    "aifm_prefetch_us": float(r["aifm_prefetch_us"]),
                    "loads_no_prefetch": float(r["loads_no_prefetch"]),
                    "loads_prefetch": float(r["loads_prefetch"]),
                    "speedup_on_vs_off": float(r["speedup_on_vs_off"]),
                }
            )
    for k in out:
        out[k].sort(key=lambda x: x["N"])
    return out


def plot_backend(rows, output: Path, backend_label: str):
    xs = [r["N"] for r in rows]
    local = [r["local_bfs_us"] for r in rows]
    off = [r["aifm_no_prefetch_us"] for r in rows]
    on = [r["aifm_prefetch_us"] for r in rows]
    speedup = [r["speedup_on_vs_off"] for r in rows]
    loads_off = [r["loads_no_prefetch"] for r in rows]
    loads_on = [r["loads_prefetch"] for r in rows]

    fig, (ax0, ax1) = plt.subplots(2, 1, figsize=(10, 8))
    fig.suptitle(f"AIFM Graph BFS ({backend_label})")

    ax0.plot(xs, off, "D-", color="#d62728", label="AIFM graph BFS (prefetch OFF)")
    ax0.plot(xs, on, "o--", color="#1f77b4", label="AIFM graph BFS (prefetch ON)")
    ax0.plot(xs, local, "s-.", color="#2ca02c", label="Local adjacency-list BFS")
    ax0.set_xscale("log", basex=2)
    ax0.set_yscale("log")
    ax0.set_xlabel("Graph size N (nodes)")
    ax0.set_ylabel("BFS latency (microseconds)")
    ax0.set_title("BFS latency vs N")
    ax0.grid(True, which="both", linestyle=":", alpha=0.5)
    ax0.legend(loc="best")

    ax1.plot(xs, speedup, "o-", color="#9467bd", label="speedup = OFF / ON")
    ax1.set_xscale("log", basex=2)
    ax1.set_xlabel("Graph size N (nodes)")
    ax1.set_ylabel("Speedup")
    ax1.grid(True, which="both", linestyle=":", alpha=0.5)
    ax1.set_title("Prefetch speedup and chunk-deref proxy")

    ax1b = ax1.twinx()
    ax1b.plot(xs, loads_off, "x--", color="#7f7f7f", label="chunk loads OFF")
    ax1b.plot(xs, loads_on, "x-.", color="#17becf", label="chunk loads ON")
    ax1b.set_ylabel("chunk_load_count (proxy)")

    h1, l1 = ax1.get_legend_handles_labels()
    h2, l2 = ax1b.get_legend_handles_labels()
    ax1.legend(h1 + h2, l1 + l2, loc="best")

    fig.tight_layout()
    output.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(str(output), dpi=150)
    print(f"Wrote {output}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "-i",
        "--input",
        type=Path,
        default=Path(__file__).resolve().parent / "summary.csv",
    )
    parser.add_argument(
        "-o",
        "--output",
        type=Path,
        default=Path(__file__).resolve().parent / "graph_bfs.png",
    )
    parser.add_argument(
        "--backend",
        choices=["fake", "tcp"],
        default=None,
        help="Backend to plot. Default: tcp if present, otherwise fake.",
    )
    args = parser.parse_args()

    data = load_rows(args.input)
    if not data:
        raise SystemExit("No rows found in summary")

    backend = args.backend
    if backend is None:
        backend = "tcp" if "tcp" in data else "fake"
    if backend not in data:
        raise SystemExit(f"Backend {backend!r} not found in summary")

    plot_backend(data[backend], args.output, backend.upper())


if __name__ == "__main__":
    main()
