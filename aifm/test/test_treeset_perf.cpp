extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "manager.hpp"
#include "treeset_rb_baseline.hpp"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = (1ULL << 30);
constexpr uint64_t kFarMemSize = (4ULL << 30);

namespace {

vector<uint64_t> gen_keys(uint32_t n, uint64_t seed) {
  mt19937_64 rng(seed);
  set<uint64_t> u;
  while (u.size() < n) {
    u.insert(rng());
  }
  return vector<uint64_t>(u.begin(), u.end());
}

vector<uint64_t> shuffle_vec(vector<uint64_t> v, uint64_t seed) {
  mt19937_64 rng(seed);
  shuffle(v.begin(), v.end(), rng);
  return v;
}

} // namespace

class TreeSetPerfReport {
public:
  void do_work(FarMemManager *manager, const char *json_path) {
    cout << "Running " << __FILE__ << " — writing JSON to " << json_path << endl;

    ostringstream json;
    json << "{\"runs\":[";

    uint32_t sizes[] = {500, 2000, 8000};
    bool first = true;

    for (uint32_t n : sizes) {
      auto keys = gen_keys(n, 42);
      auto lookups = shuffle_vec(keys, 99);

      // std::set baseline
      uint64_t std_ins = 0;
      {
        set<uint64_t> s;
        auto t0 = microtime();
        for (auto k : keys) {
          s.insert(k);
        }
        std_ins = microtime() - t0;
      }

      uint64_t std_q = 0;
      {
        set<uint64_t> s(keys.begin(), keys.end());
        volatile uint64_t found = 0;
        auto t0 = microtime();
        for (auto k : lookups) {
          found += s.count(k);
        }
        std_q = microtime() - t0;
        (void)found;
      }

      // B+ TreeSet (AIFM optimized)
      uint64_t bp_ins = 0;
      uint64_t bp_load_ins = 0;
      {
        auto ts = manager->allocate_treeset<uint64_t>();
        ts.set_concurrent_access(false);
        ts.set_track_page_loads(true);
        ts.reset_page_load_counter();
        auto t0 = microtime();
        DerefScope scope;
        for (auto k : keys) {
          ts.insert(scope, k);
        }
        bp_ins = microtime() - t0;
        bp_load_ins = ts.page_load_count();
      }

      uint64_t bp_q = 0;
      uint64_t bp_load_q = 0;
      {
        auto ts = manager->allocate_treeset<uint64_t>();
        ts.set_concurrent_access(false);
        DerefScope scope;
        for (auto k : keys) {
          ts.insert(scope, k);
        }
        ts.set_track_page_loads(true);
        ts.reset_page_load_counter();
        volatile uint64_t found = 0;
        auto t0 = microtime();
        for (auto k : lookups) {
          found += ts.contains(scope, k);
        }
        bp_q = microtime() - t0;
        bp_load_q = ts.page_load_count();
        (void)found;
      }

      uint64_t bp_range_us = 0;
      uint64_t bp_load_range = 0;
      {
        auto ts = manager->allocate_treeset<uint64_t>();
        ts.set_concurrent_access(false);
        DerefScope scope;
        for (auto k : keys) {
          ts.insert(scope, k);
        }
        ts.set_track_page_loads(true);
        ts.reset_page_load_counter();
        uint64_t lo = keys.front();
        uint64_t hi = keys.back();
        if (lo > hi) {
          swap(lo, hi);
        }
        vector<uint64_t> out;
        auto t0 = microtime();
        ts.range_query(scope, lo, hi, &out);
        bp_range_us = microtime() - t0;
        bp_load_range = ts.page_load_count();
      }

      // RB baseline (pointer-heavy far tree)
      uint64_t rb_ins = 0;
      {
        uint8_t rb_ds;
        {
          auto holder = manager->allocate_treeset<uint64_t>();
          rb_ds = holder.ds_id();
        }
        RbTreeSetBaseline<uint64_t> ts(rb_ds);
        auto t0 = microtime();
        DerefScope scope;
        for (auto k : keys) {
          ts.insert(scope, k);
        }
        rb_ins = microtime() - t0;
      }

      uint64_t rb_q = 0;
      {
        uint8_t rb_ds;
        {
          auto holder = manager->allocate_treeset<uint64_t>();
          rb_ds = holder.ds_id();
        }
        RbTreeSetBaseline<uint64_t> ts(rb_ds);
        DerefScope scope;
        for (auto k : keys) {
          ts.insert(scope, k);
        }
        volatile uint64_t found = 0;
        auto t0 = microtime();
        for (auto k : lookups) {
          found += ts.contains(scope, k);
        }
        rb_q = microtime() - t0;
        (void)found;
      }

      if (!first) {
        json << ",";
      }
      first = false;

      json << "{\"n\":" << n << ",\"std_set_insert_us\":" << std_ins
           << ",\"std_set_lookup_us\":" << std_q << ",\"bplus_insert_us\":"
           << bp_ins << ",\"bplus_lookup_us\":" << bp_q
           << ",\"bplus_range_us\":" << bp_range_us
           << ",\"bplus_page_loads_after_insert\":" << bp_load_ins
           << ",\"bplus_page_loads_lookup\":" << bp_load_q
           << ",\"bplus_page_loads_range\":" << bp_load_range
           << ",\"rb_insert_us\":" << rb_ins << ",\"rb_lookup_us\":" << rb_q
           << "}";
    }

    json << "]}";

    string out = json.str();
    ofstream f(json_path);
    if (f) {
      f << out;
    }

    cout << out << endl;
    cout << "Done." << endl;
  }
};

void _main(void *arg) {
  struct Arg {
    const char *json_path;
  };
  Arg *a = static_cast<Arg *>(arg);
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, 12, new FakeDevice(kFarMemSize)));
  TreeSetPerfReport test;
  test.do_work(manager.get(), a->json_path ? a->json_path
                                             : "treeset_perf_report.json");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "usage: " << argv[0] << " [cfg_file] [report.json]" << endl;
    return -EINVAL;
  }

  static const char *json_path = "treeset_perf_report.json";
  if (argc >= 3) {
    json_path = argv[2];
  }

  struct Arg {
    const char *json_path;
  } arg{json_path};

  int ret = runtime_init(argv[1], _main, &arg);
  if (ret) {
    cerr << "failed to start runtime" << endl;
    return ret;
  }

  return 0;
}
