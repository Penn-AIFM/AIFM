#!/bin/bash

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT="${SCRIPT_DIR}/summary.csv"

python3 - "${SCRIPT_DIR}" "${OUT}" <<'PY'
import csv
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
out = Path(sys.argv[2])
pat = re.compile(r"RESULT\s+(.*)")
kv_pat = re.compile(r"([a-zA-Z0-9_]+)=([^\s]+)")

rows = []
for p in sorted(root.glob("log.*.*")):
    text = p.read_text(errors="ignore").splitlines()
    for line in text:
        m = pat.search(line)
        if not m:
            continue
        kv = {k: v for k, v in kv_pat.findall(m.group(1))}
        if not kv:
            continue
        rows.append(kv)

if not rows:
    raise SystemExit("No RESULT lines found in logs")

cols = [
    "backend",
    "N",
    "edges",
    "trials",
    "local_bfs_us",
    "aifm_no_prefetch_us",
    "aifm_prefetch_us",
    "reachable",
    "loads_no_prefetch",
    "loads_prefetch",
    "speedup_on_vs_off",
]

rows.sort(key=lambda r: (r.get("backend", ""), int(r.get("N", "0"))))

with out.open("w", newline="") as f:
    w = csv.DictWriter(f, fieldnames=cols)
    w.writeheader()
    for r in rows:
        w.writerow({c: r.get(c, "") for c in cols})

print(f"Wrote {out}")
PY
