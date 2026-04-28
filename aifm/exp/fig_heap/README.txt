The goal of this experiment is to compare AIFM's remote-backed min-heap
(far_memory::Heap<int, MaxN>, see aifm/inc/heap.hpp) against a purely local
std::make_heap-style baseline (std::push_heap/std::pop_heap on std::vector<int>).

Both backends run the same heap-sort workload on N pseudo-random ints (fixed
seed, same distribution):
  1. Build:  push N random ints onto a min-heap.
  2. Drain:  pop N elements in ascending order, asserting monotonicity.
Each run prints a single line:

  N=<N> build_us=<us> drain_us=<us> total_us=<us>

The aifm/ subfolder uses TCPDevice, so remote memory must be configured the
same way as other TCP experiments (see aifm/configs/ssh and the root README).
The local_only/ subfolder is a plain C++ program with no Shenango runtime;
we pin it to one core with taskset for stable timing.

Usage:

  1. Edit N_arr in run.sh to the heap sizes you want to sweep (each must be
     <= kMaxN defined in aifm/main.cpp and local_only/main.cpp).
  2. Ensure the remote mem server is reachable (see aifm/configs/ssh).
  3. Run:
       ./run.sh
     It produces log.aifm.<N> and log.local.<N> for each N in the sweep.
  4. Merge into a summary CSV (backend,N,build_us,drain_us,total_us):
       ./parse.sh
  5. Plot with matplotlib (needs Python 3 + matplotlib; see requirements.txt):
       python3 plot.py
     Writes heap_total_us.png (total_us vs N, log-log). Use -i/-o to pick
     input CSV and output image paths.

As with other experiments in aifm/exp/, absolute numbers depend on hardware
and configuration; the trend (remote overhead vs local) is what the figure
illustrates.
