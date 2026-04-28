#!/usr/bin/env python3
"""
Plot TreeSet benchmark JSON produced by bin/test_treeset_perf.

Preferred: matplotlib (see scripts/requirements-plot.txt). If pip tries to compile
Pillow from source and fails, use binary wheels only, e.g.:
  python3 -m pip install --user --only-binary :all: "matplotlib>=3.3,<3.6"

If matplotlib is unavailable, this script falls back to a pure-Python SVG
(same data, open in a browser or convert to PNG).

Example:
  ./bin/test_treeset_perf ../test/runtime_local.cfg results.json
  python3 scripts/plot_treeset_benchmark.py results.json -o performance.png
  # With no arguments, uses the bundled sample under test/fixtures/
"""

import argparse
import json
import math
import sys
from pathlib import Path


# Fixed colors (do not rely on matplotlib's default cycle — second series was orange, third green).
_COLOR_STD = "#1f77b4"  # blue — std::set (local DRAM)
_COLOR_BPLUS = "#2ca02c"  # green — AIFM B+ TreeSet (page-based far memory)
_COLOR_RB = "#ff7f0e"  # orange — RB baseline: pointer-per-node far RB tree (treeset_rb_baseline)


def _default_json_path():
    # aifm/scripts/thisfile.py -> aifm/test/fixtures/...
    return (
        Path(__file__).resolve().parent.parent
        / "test"
        / "fixtures"
        / "treeset_perf_report_sample.json"
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "json_path",
        nargs="?",
        default=None,
        help="JSON from test_treeset_perf. "
        "Omitted: uses test/fixtures/treeset_perf_report_sample.json (bundled), "
        "or ./treeset_perf_report.json in the current directory if present.",
    )
    parser.add_argument(
        "-o",
        "--output",
        default="performance.png",
        help="Output image path",
    )
    args = parser.parse_args()

    if args.json_path is not None:
        path = Path(args.json_path)
    else:
        cwd_report = Path("treeset_perf_report.json")
        sample = _default_json_path()
        if cwd_report.is_file():
            path = cwd_report
        elif sample.is_file():
            path = sample
        else:
            print(
                "error: no input JSON. Either:\n"
                "  • run from aifm/: ./bin/test_treeset_perf <cfg> treeset_perf_report.json, or\n"
                f"  • pass a file: python3 {Path(__file__).name} path/to/report.json",
                file=sys.stderr,
            )
            return 1
    if not path.is_file():
        print(f"error: file not found: {path}", file=sys.stderr)
        return 1

    data = json.loads(path.read_text())
    runs = data.get("runs", [])
    if not runs:
        print("error: no 'runs' in JSON", file=sys.stderr)
        return 1

    ns = [r["n"] for r in runs]
    std_i = [r["std_set_insert_us"] for r in runs]
    bp_i = [r["bplus_insert_us"] for r in runs]
    rb_i = [r["rb_insert_us"] for r in runs]
    std_q = [r["std_set_lookup_us"] for r in runs]
    bp_q = [r["bplus_lookup_us"] for r in runs]
    rb_q = [r["rb_lookup_us"] for r in runs]
    bp_load_ins = [r.get("bplus_page_loads_after_insert", 0) for r in runs]

    def tput(us_list):
        out = []
        for n, us in zip(ns, us_list):
            out.append((n * 1e6 / us) if us > 0 else 0.0)
        return out

    out_path = Path(args.output)
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        svg_path = out_path
        if svg_path.suffix.lower() in (".png", ".pdf"):
            svg_path = out_path.with_suffix(".svg")
        _write_svg(
            svg_path,
            ns,
            std_i,
            bp_i,
            rb_i,
            std_q,
            bp_q,
            rb_q,
            tput(std_i),
            tput(bp_i),
            tput(rb_i),
            bp_load_ins,
        )
        print(
            "note: matplotlib not installed; wrote SVG (stdlib only). "
            "Install: python3 -m pip install --user --only-binary :all: "
            "'matplotlib>=3.3,<3.6'  (see scripts/requirements-plot.txt)",
            file=sys.stderr,
        )
        print(f"wrote {svg_path.resolve()}")
        return 0

    fig, axes = plt.subplots(3, 1, figsize=(9, 10))
    fig.suptitle("AIFM TreeSet (B+ pages) vs baselines")

    ax0 = axes[0]
    ax0.plot(ns, std_i, "o-", color=_COLOR_STD, label="std::set insert")
    ax0.plot(
        ns,
        bp_i,
        "s-",
        color=_COLOR_BPLUS,
        label="B+ TreeSet insert (far pages)",
    )
    ax0.plot(
        ns,
        rb_i,
        "^-",
        color=_COLOR_RB,
        label="RB baseline insert (far ptrs)",
    )
    ax0.set_xlabel("number of keys (N)")
    ax0.set_ylabel("latency (µs)")
    ax0.set_title("Insert latency vs N")
    ax0.legend()
    ax0.grid(True, alpha=0.3)

    ax1 = axes[1]
    ax1.plot(ns, std_q, "o-", color=_COLOR_STD, label="std::set lookup")
    ax1.plot(ns, bp_q, "s-", color=_COLOR_BPLUS, label="B+ TreeSet lookup")
    ax1.plot(ns, rb_q, "^-", color=_COLOR_RB, label="RB baseline lookup")
    ax1.set_xlabel("number of keys (N)")
    ax1.set_ylabel("latency (µs)")
    ax1.set_title("Lookup latency vs N")
    ax1.legend()
    ax1.grid(True, alpha=0.3)

    ax2 = axes[2]
    ax2.plot(
        ns,
        tput(std_i),
        "o-",
        color=_COLOR_STD,
        label="std::set insert throughput",
    )
    ax2.plot(
        ns,
        tput(bp_i),
        "s-",
        color=_COLOR_BPLUS,
        label="B+ TreeSet insert throughput",
    )
    ax2.plot(
        ns,
        tput(rb_i),
        "^-",
        color=_COLOR_RB,
        label="RB baseline insert throughput",
    )
    ax2.set_xlabel("number of keys (N)")
    ax2.set_ylabel("throughput (inserts/sec)")
    ax2.set_title("Insert throughput ≈ N / latency")
    ax2.grid(True, alpha=0.3)

    if any(bp_load_ins):
        ax2b = ax2.twinx()
        ax2b.plot(ns, bp_load_ins, "x--", color="gray", alpha=0.7, label="B+ page deref count (insert)")
        ax2b.set_ylabel("page_load_count (proxy for far reads)", color="gray")
        h1, l1 = ax2.get_legend_handles_labels()
        h2, l2 = ax2b.get_legend_handles_labels()
        ax2.legend(h1 + h2, l1 + l2, loc="upper left")
    else:
        ax2.legend()

    plt.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"wrote {out_path.resolve()}")
    return 0


def _y_range(*series):
    flat = [x for s in series for x in s if not math.isnan(x)]
    if not flat:
        return 0.0, 1.0
    lo, hi = min(flat), max(flat)
    if lo == hi:
        return lo - 1, hi + 1
    p = 0.05 * (hi - lo)
    return lo - p, hi + p


def _x_px(ni, n0, n1, left, right):
    if n1 == n0:
        return (left + right) / 2.0
    return left + (ni - n0) / (n1 - n0) * (right - left)


def _y_px(v, v0, v1, top, bottom):
    if v1 == v0:
        return (top + bottom) / 2.0
    return top + (v1 - v) / (v1 - v0) * (bottom - top)


def _polyline_pts(xs, ys, x0, x1, y0, y1, left, right, top, bottom):
    parts = []
    for xi, yi in zip(xs, ys):
        x = _x_px(xi, x0, x1, left, right)
        y = _y_px(yi, y0, y1, top, bottom)
        parts.append(f"{x:.1f},{y:.1f}")
    return " ".join(parts)


def _write_svg(
    path,
    ns,
    std_i,
    bp_i,
    rb_i,
    std_q,
    bp_q,
    rb_q,
    t_std,
    t_bp,
    t_rb,
    bp_loads,
):
    w, h = 900, 1000
    m = 72
    pw = w - 2 * m
    ph = 250
    gap = 20

    panels = [
        (0, "Insert latency (µs)", ns, [
            (std_i, _COLOR_STD, "std::set insert"),
            (bp_i, _COLOR_BPLUS, "B+ TreeSet insert"),
            (rb_i, _COLOR_RB, "RB baseline insert"),
        ], None),
        (1, "Lookup latency (µs)", ns, [
            (std_q, _COLOR_STD, "std::set lookup"),
            (bp_q, _COLOR_BPLUS, "B+ TreeSet lookup"),
            (rb_q, _COLOR_RB, "RB baseline lookup"),
        ], None),
        (2, "Insert throughput (ops/s)", ns, [
            (t_std, _COLOR_STD, "std::set"),
            (t_bp, _COLOR_BPLUS, "B+ TreeSet"),
            (t_rb, _COLOR_RB, "RB baseline"),
        ], bp_loads if any(bp_loads) else None),
    ]

    n0, n1 = min(ns), max(ns)
    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}"'
        f' viewBox="0 0 {w} {h}">',
        '<rect fill="white" x="0" y="0" width="%d" height="%d"/>' % (w, h),
        '<text x="450" y="28" text-anchor="middle" font-size="16" font-family="sans-serif">'
        "AIFM TreeSet (B+ pages) vs baselines</text>",
    ]

    for idx, title, xvals, series_list, load_extra in panels:
        top = 48 + idx * (ph + gap)
        left, right = m, m + pw
        bottom = top + ph
        y0, y1 = _y_range(*[s[0] for s in series_list])
        if load_extra is not None and idx == 2:
            ly0, ly1 = _y_range(load_extra)
        else:
            ly0, ly1 = None, None
        # Axes
        lines.append(
            f'<g stroke="#ccc" fill="none">'
            f'<line x1="{left:.0f}" y1="{top:.0f}" x2="{left:.0f}" y2="{bottom:.0f}"/>'
            f'<line x1="{left:.0f}" y1="{bottom:.0f}" x2="{right:.0f}" y2="{bottom:.0f}"/>'
            f"</g>"
        )
        lines.append(
            f'<text x="{w/2:.0f}" y="{top+18:.0f}" text-anchor="middle" font-size="13" '
            f'font-family="sans-serif">{_esc(title)}</text>'
        )
        # Series
        for yvals, color, label in series_list:
            pts = _polyline_pts(xvals, yvals, n0, n1, y0, y1, left, right, top, bottom)
            lines.append(
                f'<polyline fill="none" stroke="{color}" stroke-width="2" points="{pts}"/>'
            )
        if load_extra is not None and ly0 is not None and idx == 2:
            r2 = m + pw + 8
            l2, r2i = m, m + pw
            t2, b2 = top, bottom
            pts2 = _polyline_pts(
                xvals, load_extra, n0, n1, ly0, ly1, l2, r2i, t2, b2
            )
            lines.append(
                f'<polyline fill="none" stroke="#7f7f7f" stroke-dasharray="6,4" '
                f'stroke-width="1.5" points="{pts2}"/>'
            )
        # Legend
        leg_y = top + 36
        lx = m + 8
        for j, (yvals, color, label) in enumerate(series_list):
            lines.append(
                f'<line x1="{lx:.0f}" y1="{leg_y - 2:.0f}" x2="{lx+14:.0f}" y2="{leg_y - 2:.0f}" '
                f'stroke="{color}" stroke-width="3"/>'
                f'<text x="{lx+18:.0f}" y="{leg_y:.0f}" font-size="11" font-family="sans-serif">'
                f"{_esc(label)}</text>"
            )
            lx += 190
            if lx > w - 120:
                lx = m + 8
                leg_y += 14
        if load_extra is not None and idx == 2:
            lines.append(
                f'<text x="{m:.0f}" y="{bottom + 20:.0f}" font-size="10" fill="#666" '
                f'font-family="sans-serif">'
                f"gray dashed: B+ page deref count (if present)</text>"
            )

    lines.append("</svg>")
    path.write_text("\n".join(lines))


def _esc(s):
    return (
        s.replace("&", "&amp;")
        .replace("<", "&lt;")
        .replace(">", "&gt;")
        .replace('"', "&quot;")
    )


if __name__ == "__main__":
    raise SystemExit(main())
