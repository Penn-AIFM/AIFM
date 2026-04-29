The goal of this experiment is to provide a fig9-style comparison for bitmap
lookups under skewed access distributions (high Zipf skew).

It compares two backends:
  - aifm/: far_memory::Bitmap over TCPDevice (remote-backed).
  - linux_mem/: far_memory::Bitmap over FakeDevice (local-memory baseline).

Both variants:
  1. Seed a deterministic set of bits.
  2. Generate per-core Zipf-distributed bit indices.
  3. Run test()-only lookup loops and report steady-state throughput (MOPS).

The run.sh script in each subfolder sweeps the Zipf skew parameter and writes
results as log.<zipf_s>, similar to fig9.

Usage:
  1. Ensure remote memory server settings are configured in:
       aifm/configs/ssh
  2. Run either backend:
       ./aifm/run.sh
       ./linux_mem/run.sh
  3. Parse logs into a CSV summary:
       ./parse.sh
  4. Plot the comparison curve (Python 3 + matplotlib):
       python3 plot.py
     This writes bitmap_zipf_mops.png.
  5. Compare mops from summary/high-skew points (e.g., 0.9+).

As with other experiments, absolute numbers depend on hardware and setup; the
main signal is the relative trend across skew levels.
