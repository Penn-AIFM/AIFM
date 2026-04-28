#!/usr/bin/env python3
"""
Plot Deque benchmark JSON produced by bin/test_deque_perf.

Usage:
  sudo ./bin/test_deque_perf configs/client.config deque_perf_report.json
  python3 scripts/plot_deque_perf.py deque_perf_report.json -o performance.png

Falls back to pure-Python SVG if matplotlib is unavailable.
"""

import argparse
import json
import math
import sys
from pathlib import Path

_PATTERNS = [
    ("fifo",  "push_back+pop_front (FIFO)", "#1f77b4"),
    ("rfifo", "push_front+pop_back (rFIFO)", "#2ca02c"),
    ("lifo",  "push_back+pop_back (LIFO)",  "#ff7f0e"),
    ("mixed", "mixed push+pop",              "#d62728"),
]


def _load(path):
    data = json.loads(Path(path).read_text())
    runs = data.get("runs", [])
    if not runs:
        print(f"error: no 'runs' in {path}", file=sys.stderr)
        sys.exit(1)
    return runs


def _tput(n_list, us_list):
    # each pattern does 2N ops (N push + N pop)
    return [2 * n * 1e6 / us if us > 0 else 0.0 for n, us in zip(n_list, us_list)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("json_path", nargs="?", default="deque_perf_report.json")
    parser.add_argument("-o", "--output", default="performance.png")
    args = parser.parse_args()

    runs = _load(args.json_path)
    ns = [r["n"] for r in runs]

    std = {k: [r[f"std_{k}_us"] for r in runs] for k, *_ in _PATTERNS}
    fm  = {k: [r[f"fm_{k}_us"]  for r in runs] for k, *_ in _PATTERNS}

    out_path = Path(args.output)

    try:
        import matplotlib.pyplot as plt
        import matplotlib.ticker as ticker
        _plot_mpl(plt, ticker, ns, std, fm, out_path)
    except ImportError:
        svg_path = out_path.with_suffix(".svg") if out_path.suffix.lower() in (".png", ".pdf") else out_path
        _plot_svg(svg_path, ns, std, fm)
        print(
            "note: matplotlib not installed; wrote SVG.\n"
            "Install: python3 -m pip install --user --only-binary :all: 'matplotlib>=3.3,<3.6'",
            file=sys.stderr,
        )
        print(f"wrote {svg_path.resolve()}")


def _plot_mpl(plt, ticker, ns, std, fm, out_path):
    fig, axes = plt.subplots(3, 1, figsize=(9, 11))
    fig.suptitle("AIFM Deque<T> vs std::deque", fontsize=14)

    # --- Panel 1: latency comparison per pattern ---
    ax = axes[0]
    for key, label, color in _PATTERNS:
        ax.plot(ns, std[key], "o--", color=color, alpha=0.5, label=f"std::{label}")
        ax.plot(ns, fm[key],  "s-",  color=color,             label=f"Deque<T> {label}")
    ax.set_xlabel("N (elements)")
    ax.set_ylabel("latency (µs)")
    ax.set_title("Latency: std::deque (dashed) vs Deque<T> (solid)")
    ax.legend(fontsize=7, ncol=2)
    ax.grid(True, alpha=0.3)

    # --- Panel 2: throughput (2N ops / us) ---
    ax = axes[1]
    for key, label, color in _PATTERNS:
        ax.plot(ns, _tput(ns, std[key]), "o--", color=color, alpha=0.5, label=f"std::{label}")
        ax.plot(ns, _tput(ns, fm[key]),  "s-",  color=color,             label=f"Deque<T> {label}")
    ax.set_xlabel("N (elements)")
    ax.set_ylabel("throughput (ops/sec)")
    ax.set_title("Throughput = 2N ops / latency")
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(lambda x, _: f"{x/1e6:.1f}M" if x >= 1e6 else f"{x/1e3:.0f}K"))
    ax.legend(fontsize=7, ncol=2)
    ax.grid(True, alpha=0.3)

    # --- Panel 3: overhead ratio (fm / std) ---
    ax = axes[2]
    for key, label, color in _PATTERNS:
        ratios = [f / s if s > 0 else 0.0 for f, s in zip(fm[key], std[key])]
        ax.plot(ns, ratios, "s-", color=color, label=label)
    ax.axhline(1.0, color="black", linewidth=0.8, linestyle="--", alpha=0.5, label="1× (no overhead)")
    ax.set_xlabel("N (elements)")
    ax.set_ylabel("Deque<T> latency / std::deque latency")
    ax.set_title("Overhead ratio (lower = better; 1.0 = same as std)")
    ax.legend(fontsize=8)
    ax.grid(True, alpha=0.3)

    plt.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"wrote {out_path.resolve()}")


# ── Pure-Python SVG fallback ──────────────────────────────────────────────────

def _y_range(*series):
    flat = [x for s in series for x in s if not math.isnan(x)]
    if not flat:
        return 0.0, 1.0
    lo, hi = min(flat), max(flat)
    if lo == hi:
        return lo - 1, hi + 1
    p = 0.05 * (hi - lo)
    return lo - p, hi + p


def _xpx(v, v0, v1, left, right):
    return left if v1 == v0 else left + (v - v0) / (v1 - v0) * (right - left)


def _ypx(v, v0, v1, top, bottom):
    return (top + bottom) / 2 if v1 == v0 else top + (v1 - v) / (v1 - v0) * (bottom - top)


def _pts(xs, ys, x0, x1, y0, y1, left, right, top, bottom):
    return " ".join(
        f"{_xpx(x, x0, x1, left, right):.1f},{_ypx(y, y0, y1, top, bottom):.1f}"
        for x, y in zip(xs, ys)
    )


def _esc(s):
    return s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")


def _plot_svg(path, ns, std, fm):
    W, H, M = 900, 1050, 72
    PW, PH, GAP = W - 2 * M, 250, 20
    n0, n1 = min(ns), max(ns)
    left, right = M, M + PW

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{H}" viewBox="0 0 {W} {H}">',
        f'<rect fill="white" x="0" y="0" width="{W}" height="{H}"/>',
        f'<text x="{W//2}" y="28" text-anchor="middle" font-size="16" font-family="sans-serif">'
        'AIFM Deque&lt;T&gt; vs std::deque</text>',
    ]

    def panel(idx, title, series):
        top = 48 + idx * (PH + GAP)
        bottom = top + PH
        y0, y1 = _y_range(*[s for _, s, _ in series])
        lines.append(
            f'<g stroke="#ccc" fill="none">'
            f'<line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}"/>'
            f'<line x1="{left}" y1="{bottom}" x2="{right}" y2="{bottom}"/></g>'
        )
        lines.append(
            f'<text x="{W//2}" y="{top+18}" text-anchor="middle" font-size="13" '
            f'font-family="sans-serif">{_esc(title)}</text>'
        )
        lx, ly = left + 8, top + 34
        for label, ys, color in series:
            pts = _pts(ns, ys, n0, n1, y0, y1, left, right, top, bottom)
            dash = 'stroke-dasharray="6,3"' if "std::" in label else ""
            lines.append(
                f'<polyline fill="none" stroke="{color}" stroke-width="2" {dash} points="{pts}"/>'
            )
            lines.append(
                f'<line x1="{lx}" y1="{ly-3}" x2="{lx+12}" y2="{ly-3}" stroke="{color}" stroke-width="2" {dash}/>'
                f'<text x="{lx+16}" y="{ly}" font-size="10" font-family="sans-serif">{_esc(label)}</text>'
            )
            lx += 210
            if lx > W - 100:
                lx, ly = left + 8, ly + 13

    # Panel 1: latency
    lat_series = []
    for key, label, color in _PATTERNS:
        lat_series.append((f"std::{label}", std[key], color))
        lat_series.append((f"Deque<T> {label}", fm[key], color))
    panel(0, "Latency (µs): std::deque (dashed) vs Deque<T> (solid)", lat_series)

    # Panel 2: throughput
    tput_series = []
    for key, label, color in _PATTERNS:
        tput_series.append((f"std::{label}", _tput(ns, std[key]), color))
        tput_series.append((f"Deque<T> {label}", _tput(ns, fm[key]), color))
    panel(1, "Throughput (ops/sec) = 2N / latency", tput_series)

    # Panel 3: overhead ratio
    ratio_series = []
    for key, label, color in _PATTERNS:
        ratios = [f / s if s > 0 else 0.0 for f, s in zip(fm[key], std[key])]
        ratio_series.append((label, ratios, color))
    ratio_series.append(("1× baseline", [1.0] * len(ns), "#888888"))
    panel(2, "Overhead ratio: Deque<T> / std::deque (1.0 = no overhead)", ratio_series)

    lines.append("</svg>")
    path.write_text("\n".join(lines))


if __name__ == "__main__":
    main()
