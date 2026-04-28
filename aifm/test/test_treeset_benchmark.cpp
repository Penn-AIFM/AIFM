extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "manager.hpp"

#include <algorithm>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <set>
#include <vector>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = (1ULL << 30);
constexpr uint64_t kFarMemSize = (4ULL << 30);
constexpr uint32_t kNumGCThreads = 12;
constexpr uint32_t kNumWarmupOps = 100;

namespace far_memory {

class FarMemTest {
public:
  static void print_header() {
    cout << left << setw(12) << "N" << setw(16) << "Operation" << setw(20)
         << "std::set (us)" << setw(20) << "TreeSet (us)" << setw(12)
         << "Ratio" << endl;
    cout << string(80, '-') << endl;
  }

  static void print_row(uint32_t n, const char *op, uint64_t std_us,
                         uint64_t ts_us) {
    double ratio = (std_us > 0) ? (double)ts_us / std_us : 0.0;
    cout << left << setw(12) << n << setw(16) << op << setw(20) << std_us
         << setw(20) << ts_us << setw(12) << fixed << setprecision(2) << ratio
         << endl;
  }

  static vector<uint64_t> generate_keys(uint32_t n, uint64_t seed = 42) {
    mt19937_64 rng(seed);
    set<uint64_t> unique_keys;
    while (unique_keys.size() < n) {
      unique_keys.insert(rng());
    }
    return vector<uint64_t>(unique_keys.begin(), unique_keys.end());
  }

  static vector<uint64_t> shuffle_keys(const vector<uint64_t> &keys,
                                        uint64_t seed = 99) {
    vector<uint64_t> shuffled = keys;
    mt19937_64 rng(seed);
    shuffle(shuffled.begin(), shuffled.end(), rng);
    return shuffled;
  }

  // ---- std::set benchmarks ----

  static uint64_t bench_std_insert(const vector<uint64_t> &keys) {
    set<uint64_t> s;
    auto start = microtime();
    for (auto k : keys) {
      s.insert(k);
    }
    auto end = microtime();
    return end - start;
  }

  static uint64_t bench_std_contains(set<uint64_t> &s,
                                      const vector<uint64_t> &keys) {
    volatile uint64_t found = 0;
    auto start = microtime();
    for (auto k : keys) {
      found += (s.count(k) > 0);
    }
    auto end = microtime();
    return end - start;
  }

  static uint64_t bench_std_iterate(set<uint64_t> &s) {
    volatile uint64_t sum = 0;
    auto start = microtime();
    for (auto &v : s) {
      sum += v;
    }
    auto end = microtime();
    return end - start;
  }

  static uint64_t bench_std_remove(set<uint64_t> &s,
                                    const vector<uint64_t> &keys) {
    auto start = microtime();
    for (auto k : keys) {
      s.erase(k);
    }
    auto end = microtime();
    return end - start;
  }

  // ---- AIFM TreeSet benchmarks ----

  static uint64_t bench_treeset_insert(TreeSet<uint64_t> &ts,
                                        const vector<uint64_t> &keys) {
    auto start = microtime();
    for (auto k : keys) {
      DerefScope scope;
      ts.insert(scope, k);
    }
    auto end = microtime();
    return end - start;
  }

  static uint64_t bench_treeset_contains(TreeSet<uint64_t> &ts,
                                          const vector<uint64_t> &keys) {
    volatile uint64_t found = 0;
    auto start = microtime();
    for (auto k : keys) {
      DerefScope scope;
      found += ts.contains(scope, k);
    }
    auto end = microtime();
    return end - start;
  }

  static uint64_t bench_treeset_iterate(TreeSet<uint64_t> &ts) {
    volatile uint64_t sum = 0;
    auto start = microtime();
    {
      DerefScope scope;
      for (auto it = ts.begin(scope); it != ts.end(scope); it.inc(scope)) {
        sum += it.deref(scope);
      }
    }
    auto end = microtime();
    return end - start;
  }

  static uint64_t bench_treeset_remove(TreeSet<uint64_t> &ts,
                                        const vector<uint64_t> &keys) {
    auto start = microtime();
    for (auto k : keys) {
      DerefScope scope;
      ts.remove(scope, k);
    }
    auto end = microtime();
    return end - start;
  }

  void run_benchmark(FarMemManager *manager, uint32_t n) {
    auto keys = generate_keys(n);
    auto lookup_keys = shuffle_keys(keys);
    auto remove_keys = shuffle_keys(keys, 123);

    // --- std::set ---
    uint64_t std_insert_us = bench_std_insert(keys);

    set<uint64_t> std_set(keys.begin(), keys.end());
    uint64_t std_contains_us = bench_std_contains(std_set, lookup_keys);
    uint64_t std_iterate_us = bench_std_iterate(std_set);
    uint64_t std_remove_us = bench_std_remove(std_set, remove_keys);

    // --- AIFM TreeSet ---
    auto ts = manager->allocate_treeset<uint64_t>();
    ts.set_concurrent_access(false);
    ts.set_track_page_loads(false);

    uint64_t ts_insert_us = bench_treeset_insert(ts, keys);
    uint64_t ts_contains_us = bench_treeset_contains(ts, lookup_keys);
    uint64_t ts_iterate_us = bench_treeset_iterate(ts);
    uint64_t ts_remove_us = bench_treeset_remove(ts, remove_keys);

    TEST_ASSERT(ts.empty());

    print_row(n, "insert", std_insert_us, ts_insert_us);
    print_row(n, "contains", std_contains_us, ts_contains_us);
    print_row(n, "iterate", std_iterate_us, ts_iterate_us);
    print_row(n, "remove", std_remove_us, ts_remove_us);
  }

  void do_work(FarMemManager *manager) {
    cout << "Running " << __FILE__ "..." << endl;

    // Warmup: small insert/remove cycle to prime allocator paths
    {
      auto warmup = manager->allocate_treeset<uint64_t>();
      warmup.set_concurrent_access(false);
      warmup.set_track_page_loads(false);
      DerefScope scope;
      for (uint32_t i = 0; i < kNumWarmupOps; i++) {
        warmup.insert(scope, (uint64_t)i);
      }
      for (uint32_t i = 0; i < kNumWarmupOps; i++) {
        warmup.remove(scope, (uint64_t)i);
      }
    }

    cout << endl;
    cout << "=== TreeSet vs std::set Benchmark ===" << endl;
    cout << "(all times in microseconds)" << endl << endl;

    print_header();

    uint32_t sizes[] = {1000, 10000, 100000, 1000000};
    for (auto n : sizes) {
      run_benchmark(manager, n);
      if (n != sizes[3]) {
        cout << string(80, '-') << endl;
      }
    }

    cout << string(80, '=') << endl;

    // Per-operation throughput summary
    cout << endl
         << "=== Throughput (ops/sec, insert+contains averaged) ===" << endl
         << endl;
    cout << left << setw(12) << "N" << setw(20) << "std::set" << setw(20)
         << "TreeSet" << endl;
    cout << string(52, '-') << endl;

    for (auto n : sizes) {
      auto keys = generate_keys(n);
      auto lookup_keys = shuffle_keys(keys);

      uint64_t std_us = bench_std_insert(keys);
      set<uint64_t> tmp_std(keys.begin(), keys.end());
      std_us += bench_std_contains(tmp_std, lookup_keys);

      auto ts = manager->allocate_treeset<uint64_t>();
      ts.set_concurrent_access(false);
      ts.set_track_page_loads(false);
      uint64_t ts_us = bench_treeset_insert(ts, keys);
      ts_us += bench_treeset_contains(ts, lookup_keys);

      double std_ops = (std_us > 0) ? (2.0 * n * 1e6) / std_us : 0;
      double ts_ops = (ts_us > 0) ? (2.0 * n * 1e6) / ts_us : 0;

      cout << left << setw(12) << n << setw(20) << fixed << setprecision(0)
           << std_ops << setw(20) << ts_ops << endl;
    }

    cout << endl << "Passed" << endl;
  }
};

} // namespace far_memory

void _main(void *arg) {
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, kNumGCThreads,
                                  new FakeDevice(kFarMemSize)));
  FarMemTest test;
  test.do_work(manager.get());
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    cerr << "usage: [cfg_file]" << endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    cerr << "failed to start runtime" << endl;
    return ret;
  }

  return 0;
}
