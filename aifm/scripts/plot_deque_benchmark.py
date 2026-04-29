#!/usr/bin/env python3
"""
Plot Deque benchmark JSON produced by bin/test_deque_perf.

Preferred: matplotlib (see scripts/requirements-plot.txt). If unavailable,
falls back to a pure-Python SVG (open in a browser or convert to PNG).

Example:
  ./bin/test_deque_perf ../test/runtime_local.cfg deque_perf_report.json
  python3 scripts/plot_deque_benchmark.py deque_perf_report.json -o performance.png
  # With no arguments, reads ./deque_perf_report.json in the current directory.
"""

import argparse
import json
import math
import sys
from pathlib import Path

_COLOR_STD      = "#1f77b4"   # blue   — std::deque
_COLOR_FM       = "#ff7f0e"   # orange — far_memory::Deque (customized_split=false)
_COLOR_FM_FAST  = "#2ca02c"   # green  — far_memory::Deque (customized_split=true)


def _tput(ns, us_list):
    return [(n * 1e6 / us) if us > 0 else 0.0 for n, us in zip(ns, us_list)]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "json_path",
        nargs="?",
        default=None,
        help="JSON from test_deque_perf (default: ./deque_perf_report.json).",
    )
    parser.add_argument(
        "-o", "--output", default="deque_performance.png",
        help="Output image path (default: deque_performance.png).",
    )
    args = parser.parse_args()

    if args.json_path is not None:
        path = Path(args.json_path)
    else:
        path = Path("deque_perf_report.json")

    if not path.is_file():
        print(
            f"error: file not found: {path}\n"
            "Run:  ./bin/test_deque_perf <cfg> deque_perf_report.json",
            file=sys.stderr,
        )
        return 1

    data = json.loads(path.read_text())
    runs = data.get("runs", [])
    if not runs:
        print("error: no 'runs' in JSON", file=sys.stderr)
        return 1

    ns = [r["n"] for r in runs]

    std_pb  = [r["std_push_back_us"]  for r in runs]
    std_pf  = [r["std_push_front_us"] for r in runs]
    std_dpf = [r["std_pop_front_us"]  for r in runs]
    std_dpb = [r["std_pop_back_us"]   for r in runs]
    std_mx  = [r["std_mixed_us"]      for r in runs]

    fm_pb   = [r["fm_push_back_us"]   for r in runs]
    fm_pf   = [r["fm_push_front_us"]  for r in runs]
    fm_dpf  = [r["fm_pop_front_us"]   for r in runs]
    fm_dpb  = [r["fm_pop_back_us"]    for r in runs]
    fm_mx   = [r["fm_mixed_us"]       for r in runs]

    fm_fast_pb  = [r.get("fm_fast_push_back_us",  0) for r in runs]
    fm_fast_pf  = [r.get("fm_fast_push_front_us", 0) for r in runs]
    fm_fast_dpf = [r.get("fm_fast_pop_front_us",  0) for r in runs]
    fm_fast_dpb = [r.get("fm_fast_pop_back_us",   0) for r in runs]
    fm_fast_mx  = [r.get("fm_fast_mixed_us",       0) for r in runs]
    has_fast = any(fm_fast_pb)

    out_path = Path(args.output)

    try:
        import matplotlib.pyplot as plt
    except ImportError:
        svg_path = out_path.with_suffix(".svg") if out_path.suffix.lower() in (".png", ".pdf") else out_path
        _write_svg(svg_path, ns,
                   std_pb, std_pf, std_dpf, std_dpb, std_mx,
                   fm_pb, fm_pf, fm_dpf, fm_dpb, fm_mx,
                   fm_fast_pb if has_fast else None,
                   fm_fast_pf if has_fast else None,
                   fm_fast_dpf if has_fast else None,
                   fm_fast_dpb if has_fast else None,
                   fm_fast_mx if has_fast else None)
        print(
            "note: matplotlib not installed; wrote SVG. "
            "Install: python3 -m pip install --user --only-binary :all: "
            "'matplotlib>=3.3,<3.6'",
            file=sys.stderr,
        )
        print(f"wrote {svg_path.resolve()}")
        return 0

    import math as _math
    xticks = sorted(set(ns))
    xlabels = [f"$2^{{{int(_math.log2(n))}}}$" if n > 0 and (n & (n-1)) == 0
               else str(n) for n in xticks]

    def _logax(ax):
        ax.set_xscale("log", base=2)
        ax.set_xticks(xticks)
        ax.set_xticklabels(xlabels)
        ax.grid(True, alpha=0.3, which="both")

    fig, axes = plt.subplots(3, 1, figsize=(9, 11))
    fig.suptitle("far_memory::Deque vs std::deque")

    # Panel 0: push latency
    ax = axes[0]
    ax.plot(ns, std_pb,  "o-",  color=_COLOR_STD,      label="std push_back")
    ax.plot(ns, std_pf,  "o--", color=_COLOR_STD,      label="std push_front")
    ax.plot(ns, fm_pb,   "s-",  color=_COLOR_FM,       label="fm push_back (split)")
    ax.plot(ns, fm_pf,   "s--", color=_COLOR_FM,       label="fm push_front (split)")
    if has_fast:
        ax.plot(ns, fm_fast_pb,  "^-",  color=_COLOR_FM_FAST, label="fm push_back (no-split)")
        ax.plot(ns, fm_fast_pf,  "^--", color=_COLOR_FM_FAST, label="fm push_front (no-split)")
    ax.set_xlabel("N (elements)")
    ax.set_ylabel("latency (µs)")
    ax.set_title("Push latency")
    ax.legend()
    _logax(ax)

    # Panel 1: pop latency
    ax = axes[1]
    ax.plot(ns, std_dpf, "o-",  color=_COLOR_STD,      label="std pop_front")
    ax.plot(ns, std_dpb, "o--", color=_COLOR_STD,      label="std pop_back")
    ax.plot(ns, fm_dpf,  "s-",  color=_COLOR_FM,       label="fm pop_front (split)")
    ax.plot(ns, fm_dpb,  "s--", color=_COLOR_FM,       label="fm pop_back (split)")
    if has_fast:
        ax.plot(ns, fm_fast_dpf, "^-",  color=_COLOR_FM_FAST, label="fm pop_front (no-split)")
        ax.plot(ns, fm_fast_dpb, "^--", color=_COLOR_FM_FAST, label="fm pop_back (no-split)")
    ax.set_xlabel("N (elements)")
    ax.set_ylabel("latency (µs)")
    ax.set_title("Pop latency")
    ax.legend()
    _logax(ax)

    # Panel 2: mixed throughput
    ax = axes[2]
    ax.plot(ns, _tput(ns, std_mx),     "o-", color=_COLOR_STD,      label="std mixed")
    ax.plot(ns, _tput(ns, fm_mx),      "s-", color=_COLOR_FM,       label="fm mixed (split)")
    if has_fast:
        ax.plot(ns, _tput(ns, fm_fast_mx), "^-", color=_COLOR_FM_FAST, label="fm mixed (no-split)")
    ax.set_xlabel("N (elements)")
    ax.set_ylabel("throughput (ops/sec)")
    ax.set_title("Mixed-op throughput (N / total_us)")
    ax.legend()
    _logax(ax)

    plt.tight_layout()
    fig.savefig(out_path, dpi=150)
    print(f"wrote {out_path.resolve()}")
    return 0


# ---------------------------------------------------------------------------
# SVG fallback
# ---------------------------------------------------------------------------

def _y_range(*series):
    flat = [x for s in series for x in s if not math.isnan(x)]
    if not flat:
        return 0.0, 1.0
    lo, hi = min(flat), max(flat)
    if lo == hi:
        return lo - 1, hi + 1
    p = 0.05 * (hi - lo)
    return lo - p, hi + p


def _x_px(xi, x0, x1, left, right):
    if x1 == x0:
        return (left + right) / 2.0
    return left + (xi - x0) / (x1 - x0) * (right - left)


def _y_px(v, y0, y1, top, bottom):
    if y1 == y0:
        return (top + bottom) / 2.0
    return top + (y1 - v) / (y1 - y0) * (bottom - top)


def _polypts(xs, ys, x0, x1, y0, y1, left, right, top, bottom):
    parts = []
    for xi, yi in zip(xs, ys):
        x = _x_px(xi, x0, x1, left, right)
        y = _y_px(yi, y0, y1, top, bottom)
        parts.append(f"{x:.1f},{y:.1f}")
    return " ".join(parts)


def _esc(s):
    return s.replace("&","&amp;").replace("<","&lt;").replace(">","&gt;").replace('"',"&quot;")


def _write_svg(path, ns,
               std_pb, std_pf, std_dpf, std_dpb, std_mx,
               fm_pb, fm_pf, fm_dpf, fm_dpb, fm_mx,
               fm_fast_pb=None, fm_fast_pf=None,
               fm_fast_dpf=None, fm_fast_dpb=None, fm_fast_mx=None):
    t_std      = _tput(ns, std_mx)
    t_fm       = _tput(ns, fm_mx)
    t_fm_fast  = _tput(ns, fm_fast_mx) if fm_fast_mx else None
    has_fast   = fm_fast_pb is not None

    w, h, m = 900, 1050, 72
    pw, ph, gap = w - 2 * m, 270, 20
    n0, n1 = min(ns), max(ns)

    push_series = [
        (std_pb,  _COLOR_STD,      "std push_back",          "-"),
        (std_pf,  _COLOR_STD,      "std push_front",         "--"),
        (fm_pb,   _COLOR_FM,       "fm push_back (split)",   "-"),
        (fm_pf,   _COLOR_FM,       "fm push_front (split)",  "--"),
    ]
    pop_series = [
        (std_dpf, _COLOR_STD,      "std pop_front",          "-"),
        (std_dpb, _COLOR_STD,      "std pop_back",           "--"),
        (fm_dpf,  _COLOR_FM,       "fm pop_front (split)",   "-"),
        (fm_dpb,  _COLOR_FM,       "fm pop_back (split)",    "--"),
    ]
    mix_series = [
        (t_std,   _COLOR_STD,      "std mixed",              "-"),
        (t_fm,    _COLOR_FM,       "fm mixed (split)",       "-"),
    ]
    if has_fast:
        push_series += [
            (fm_fast_pb, _COLOR_FM_FAST, "fm push_back (no-split)",  "-"),
            (fm_fast_pf, _COLOR_FM_FAST, "fm push_front (no-split)", "--"),
        ]
        pop_series += [
            (fm_fast_dpf, _COLOR_FM_FAST, "fm pop_front (no-split)", "-"),
            (fm_fast_dpb, _COLOR_FM_FAST, "fm pop_back (no-split)",  "--"),
        ]
        mix_series.append((t_fm_fast, _COLOR_FM_FAST, "fm mixed (no-split)", "-"))

    panels = [
        ("Push latency (µs)",      push_series),
        ("Pop latency (µs)",       pop_series),
        ("Mixed throughput (ops/s)", mix_series),
    ]

    lines = [
        '<?xml version="1.0" encoding="UTF-8"?>',
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">',
        f'<rect fill="white" x="0" y="0" width="{w}" height="{h}"/>',
        f'<text x="{w//2}" y="28" text-anchor="middle" font-size="16" font-family="sans-serif">'
        'far_memory::Deque vs std::deque</text>',
    ]

    for idx, (title, series) in enumerate(panels):
        top    = 48 + idx * (ph + gap)
        left, right, bottom = m, m + pw, top + ph
        y0, y1 = _y_range(*[s[0] for s in series])
        lines.append(
            f'<g stroke="#ccc" fill="none">'
            f'<line x1="{left}" y1="{top}" x2="{left}" y2="{bottom}"/>'
            f'<line x1="{left}" y1="{bottom}" x2="{right}" y2="{bottom}"/>'
            '</g>'
        )
        lines.append(
            f'<text x="{w//2}" y="{top+18}" text-anchor="middle" font-size="13" '
            f'font-family="sans-serif">{_esc(title)}</text>'
        )
        for yvals, color, label, dash in series:
            pts = _polypts(ns, yvals, n0, n1, y0, y1, left, right, top, bottom)
            da = ' stroke-dasharray="6,4"' if dash == "--" else ""
            lines.append(
                f'<polyline fill="none" stroke="{color}" stroke-width="2"{da} points="{pts}"/>'
            )
        lx, ly = m + 8, top + 36
        for _, color, label, dash in series:
            da = ' stroke-dasharray="6,4"' if dash == "--" else ""
            lines.append(
                f'<line x1="{lx}" y1="{ly-2}" x2="{lx+14}" y2="{ly-2}" '
                f'stroke="{color}" stroke-width="2"{da}/>'
                f'<text x="{lx+18}" y="{ly}" font-size="11" font-family="sans-serif">'
                f'{_esc(label)}</text>'
            )
            lx += 200
            if lx > w - 120:
                lx, ly = m + 8, ly + 14

    lines.append("</svg>")
    path.write_text("\n".join(lines))


if __name__ == "__main__":
    raise SystemExit(main())
