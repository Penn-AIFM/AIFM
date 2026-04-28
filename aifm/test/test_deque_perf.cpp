extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <deque>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = (1ULL << 30);
constexpr uint64_t kFarMemSize = (4ULL << 30);
constexpr uint32_t kNumGCThreads = 12;
constexpr uint32_t kScopeResetInterval = 256;

namespace far_memory {

class FarMemTest {
  static void print_header() {
    cout << left << setw(12) << "N" << setw(24) << "Pattern" << setw(20)
         << "std::deque (us)" << setw(20) << "Deque<T> (us)" << setw(10)
         << "Ratio" << endl;
    cout << string(86, '-') << endl;
  }

  static void print_row(uint32_t n, const char *pattern, uint64_t std_us,
                        uint64_t fm_us) {
    double ratio = std_us > 0 ? (double)fm_us / std_us : 0.0;
    cout << left << setw(12) << n << setw(24) << pattern << setw(20) << std_us
         << setw(20) << fm_us << setw(10) << fixed << setprecision(2) << ratio
         << endl;
  }

  struct RunResult {
    uint32_t n;
    uint64_t std_fifo_us, fm_fifo_us;
    uint64_t std_rfifo_us, fm_rfifo_us;
    uint64_t std_lifo_us, fm_lifo_us;
    uint64_t std_mixed_us, fm_mixed_us;
  };

public:
  void do_work(FarMemManager *manager, const char *json_path) {
    cout << "Running " << __FILE__ << " — writing JSON to " << json_path
         << endl;
    print_header();

    uint32_t sizes[] = {1000, 10000, 100000};
    ostringstream json;
    json << "{\"runs\":[";
    bool first = true;

    for (uint32_t N : sizes) {
      RunResult r;
      r.n = N;

      // --- push_back + pop_front (FIFO) ---
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++)
          dq.push_back(i);
        for (uint32_t i = 0; i < N; i++)
          dq.pop_front();
        r.std_fifo_us = microtime() - t0;
      }
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, i);
        }
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_front(scope);
        }
        r.fm_fifo_us = microtime() - t0;
      }
      print_row(N, "push_back+pop_front", r.std_fifo_us, r.fm_fifo_us);

      // --- push_front + pop_back (reverse FIFO) ---
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++)
          dq.push_front(i);
        for (uint32_t i = 0; i < N; i++)
          dq.pop_back();
        r.std_rfifo_us = microtime() - t0;
      }
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_front(scope, i);
        }
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_back(scope);
        }
        r.fm_rfifo_us = microtime() - t0;
      }
      print_row(N, "push_front+pop_back", r.std_rfifo_us, r.fm_rfifo_us);

      // --- push_back + pop_back (LIFO) ---
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++)
          dq.push_back(i);
        for (uint32_t i = 0; i < N; i++)
          dq.pop_back();
        r.std_lifo_us = microtime() - t0;
      }
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, i);
        }
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_back(scope);
        }
        r.fm_lifo_us = microtime() - t0;
      }
      print_row(N, "push_back+pop_back", r.std_lifo_us, r.fm_lifo_us);

      // --- Mixed: interleaved push_back+push_front, then pop_front+pop_back ---
      {
        deque<uint64_t> dq;
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++) {
          dq.push_back(i);
          dq.push_front(i);
        }
        for (uint32_t i = 0; i < N; i++) {
          dq.pop_front();
          dq.pop_back();
        }
        r.std_mixed_us = microtime() - t0;
      }
      {
        DerefScope scope;
        auto dq = manager->allocate_deque<uint64_t>(scope);
        auto t0 = microtime();
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.push_back(scope, i);
          dq.push_front(scope, i);
        }
        for (uint32_t i = 0; i < N; i++) {
          if (unlikely(i % kScopeResetInterval == 0))
            scope.renew();
          dq.pop_front(scope);
          dq.pop_back(scope);
        }
        r.fm_mixed_us = microtime() - t0;
      }
      print_row(N, "mixed push+pop", r.std_mixed_us, r.fm_mixed_us);

      if (!first)
        json << ",";
      first = false;
      json << "{\"n\":" << r.n << ",\"std_fifo_us\":" << r.std_fifo_us
           << ",\"fm_fifo_us\":" << r.fm_fifo_us
           << ",\"std_rfifo_us\":" << r.std_rfifo_us
           << ",\"fm_rfifo_us\":" << r.fm_rfifo_us
           << ",\"std_lifo_us\":" << r.std_lifo_us
           << ",\"fm_lifo_us\":" << r.fm_lifo_us
           << ",\"std_mixed_us\":" << r.std_mixed_us
           << ",\"fm_mixed_us\":" << r.fm_mixed_us << "}";
    }

    json << "]}";
    string out = json.str();
    ofstream f(json_path);
    if (f)
      f << out;
    cout << "\n" << out << endl;
    cout << "Done." << endl;
  }
};

} // namespace far_memory

void _main(void *arg) {
  struct Arg {
    const char *json_path;
  };
  Arg *a = static_cast<Arg *>(arg);
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, kNumGCThreads,
                                  new FakeDevice(kFarMemSize)));
  FarMemTest test;
  test.do_work(manager.get(), a->json_path);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "usage: " << argv[0] << " [cfg_file] [report.json]" << endl;
    return -EINVAL;
  }

  static const char *json_path = "deque_perf_report.json";
  if (argc >= 3)
    json_path = argv[2];

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
