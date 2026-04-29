extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "manager.hpp"

#include <cstdint>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = (1ULL << 30);
constexpr uint64_t kFarMemSize = (4ULL << 30);
constexpr uint32_t kNumGCThreads = 12;
constexpr uint32_t kScopeResetInterval = 256;

// Sizes chosen so each workload fits in far-memory with meaningful GC pressure.
static const uint32_t kSizes[] = {1<<15, 1<<16, 1<<17, 1<<18, 1<<19, 1<<20};

class DequePerfReport {
public:
  void do_work(FarMemManager *manager, const char *json_path) {
    cout << "Running " << __FILE__ << " — writing JSON to " << json_path
         << endl;

    ostringstream json;
    json << "{\"runs\":[";
    bool first = true;

    for (uint32_t n : kSizes) {
      // ---- std::deque baselines ----
      uint64_t std_push_back_us = 0;
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++)
          dq.push_back(i);
        std_push_back_us = microtime() - t0;
      }

      uint64_t std_push_front_us = 0;
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++)
          dq.push_front(i);
        std_push_front_us = microtime() - t0;
      }

      uint64_t std_pop_front_us = 0;
      {
        deque<uint64_t> dq;
        for (uint32_t i = 0; i < n; i++)
          dq.push_back(i);
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++)
          dq.pop_front();
        std_pop_front_us = microtime() - t0;
      }

      uint64_t std_pop_back_us = 0;
      {
        deque<uint64_t> dq;
        for (uint32_t i = 0; i < n; i++)
          dq.push_back(i);
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++)
          dq.pop_back();
        std_pop_back_us = microtime() - t0;
      }

      uint64_t std_mixed_us = 0;
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          dq.push_back(i);
          if (i % 2 == 1)
            dq.pop_front();
        }
        std_mixed_us = microtime() - t0;
      }

      // ---- far_memory::Deque benchmarks ----
      uint64_t fm_push_back_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
        }
        fm_push_back_us = microtime() - t0;
      }

      uint64_t fm_push_front_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_front(scope, static_cast<uint64_t>(i));
        }
        fm_push_front_us = microtime() - t0;
      }

      uint64_t fm_pop_front_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
        }
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_front(scope);
        }
        fm_pop_front_us = microtime() - t0;
      }

      uint64_t fm_pop_back_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
        }
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_back(scope);
        }
        fm_pop_back_us = microtime() - t0;
      }

      uint64_t fm_mixed_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        uint32_t op = 0;
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(op % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
          op++;
          if (i % 2 == 1) {
            if (unlikely(op % kScopeResetInterval == 0))
              scope.renew();
            dq.pop_front(scope);
            op++;
          }
        }
        fm_mixed_us = microtime() - t0;
      }

      // ---- far_memory::Deque (customized_split=true) — avoids data-copy on
      //      chunk overflow; same optimization used by Queue and Stack ----
      uint64_t fm_fast_push_back_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope, true);
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
        }
        fm_fast_push_back_us = microtime() - t0;
      }

      uint64_t fm_fast_push_front_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope, true);
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_front(scope, static_cast<uint64_t>(i));
        }
        fm_fast_push_front_us = microtime() - t0;
      }

      uint64_t fm_fast_pop_front_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope, true);
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
        }
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_front(scope);
        }
        fm_fast_pop_front_us = microtime() - t0;
      }

      uint64_t fm_fast_pop_back_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope, true);
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
        }
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_back(scope);
        }
        fm_fast_pop_back_us = microtime() - t0;
      }

      uint64_t fm_fast_mixed_us = 0;
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope, true);
        uint32_t op = 0;
        auto t0 = microtime();
        for (uint32_t i = 0; i < n; i++) {
          if (unlikely(op % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, static_cast<uint64_t>(i));
          op++;
          if (i % 2 == 1) {
            if (unlikely(op % kScopeResetInterval == 0))
              scope.renew();
            dq.pop_front(scope);
            op++;
          }
        }
        fm_fast_mixed_us = microtime() - t0;
      }

      if (!first)
        json << ",";
      first = false;

      json << "{\"n\":" << n << ",\"std_push_back_us\":" << std_push_back_us
           << ",\"std_push_front_us\":" << std_push_front_us
           << ",\"std_pop_front_us\":" << std_pop_front_us
           << ",\"std_pop_back_us\":" << std_pop_back_us
           << ",\"std_mixed_us\":" << std_mixed_us
           << ",\"fm_push_back_us\":" << fm_push_back_us
           << ",\"fm_push_front_us\":" << fm_push_front_us
           << ",\"fm_pop_front_us\":" << fm_pop_front_us
           << ",\"fm_pop_back_us\":" << fm_pop_back_us
           << ",\"fm_mixed_us\":" << fm_mixed_us
           << ",\"fm_fast_push_back_us\":" << fm_fast_push_back_us
           << ",\"fm_fast_push_front_us\":" << fm_fast_push_front_us
           << ",\"fm_fast_pop_front_us\":" << fm_fast_pop_front_us
           << ",\"fm_fast_pop_back_us\":" << fm_fast_pop_back_us
           << ",\"fm_fast_mixed_us\":" << fm_fast_mixed_us << "}";
    }

    json << "]}";

    string out = json.str();
    ofstream f(json_path);
    if (f)
      f << out;

    cout << out << endl;
    cout << "Passed" << endl;
  }
};

void _main(void *arg) {
  struct Arg {
    const char *json_path;
  };
  Arg *a = static_cast<Arg *>(arg);
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, kNumGCThreads,
                                  new FakeDevice(kFarMemSize)));
  DequePerfReport report;
  report.do_work(manager.get(),
                 a->json_path ? a->json_path : "deque_perf_report.json");
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "usage: " << argv[0] << " [cfg_file] [report.json]" << endl;
    return -EINVAL;
  }

  // argv[2] is the mem-server address passed by run_program; ignore it.
  static const char *json_path = "deque_perf_report.json";

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
