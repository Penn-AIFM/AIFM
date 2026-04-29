This experiment benchmarks the new AIFM Graph BFS traversal with prefetch OFF vs ON,
and compares against a local in-memory adjacency-list BFS baseline.

It produces one structured result line per run:

  RESULT backend=<fake|tcp> N=<nodes> edges=<edges> trials=<k>
         local_bfs_us=<us> aifm_no_prefetch_us=<us> aifm_prefetch_us=<us>
         reachable=<count> loads_no_prefetch=<count> loads_prefetch=<count>
         speedup_on_vs_off=<ratio>

The benchmark always uses the same generated graph for all three measurements
(local baseline, AIFM prefetch OFF, AIFM prefetch ON), so comparisons are apples-to-apples.

Files:
  - aifm/main.cpp     : benchmark binary
  - aifm/Makefile     : build benchmark
  - run.sh            : run a sweep (fake or tcp)
  - parse.sh          : collect RESULT lines into summary.csv
  - plot.py           : draw graph_bfs.png from summary.csv

Usage:
  # 1) Build+run in FakeDevice mode (default):
  ./run.sh fake

  # 2) Or run in TCP mode (needs mem server environment like other TCP experiments):
  ./run.sh tcp

  # 3) Parse logs to CSV:
  ./parse.sh

  # 4) Plot:
  python3 plot.py

Environment knobs (optional):
  EDGE_FACTOR=<int>    default 8
  TRIALS=<int>         default 5

Output:
  - log.fake.<N> or log.tcp.<N>
  - summary.csv
  - graph_bfs.png
