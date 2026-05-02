extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <cstring>
#include <deque>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace far_memory;
using namespace std;

// Small cache so larger N values spill into TCP far memory.
constexpr uint64_t kCacheSize    = (64ULL << 20);
constexpr uint64_t kFarMemSize   = (8ULL  << 30);
constexpr uint32_t kNumGCThreads = 12;
constexpr uint32_t kNumConnections = 300;
constexpr uint32_t kScopeResetInterval = 256;

static const uint32_t kSizes[] = {1<<15, 1<<16, 1<<17, 1<<18, 1<<19, 1<<20, 1<<21, 1<<22, 1<<23};

class DequePerfReport {
public:
  void do_work(FarMemManager *manager) {
    cout << "Running " << __FILE__ << " — writing JSON to tcp_deque_perf_report.json" << endl;

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

      // ---- far_memory::Deque (customized_split=false) ----
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

      // ---- far_memory::Deque (customized_split=true) ----
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

      json << "{\"n\":" << n
           << ",\"std_push_back_us\":"   << std_push_back_us
           << ",\"std_push_front_us\":"  << std_push_front_us
           << ",\"std_pop_front_us\":"   << std_pop_front_us
           << ",\"std_pop_back_us\":"    << std_pop_back_us
           << ",\"std_mixed_us\":"       << std_mixed_us
           << ",\"fm_push_back_us\":"    << fm_push_back_us
           << ",\"fm_push_front_us\":"   << fm_push_front_us
           << ",\"fm_pop_front_us\":"    << fm_pop_front_us
           << ",\"fm_pop_back_us\":"     << fm_pop_back_us
           << ",\"fm_mixed_us\":"        << fm_mixed_us
           << ",\"fm_fast_push_back_us\":"  << fm_fast_push_back_us
           << ",\"fm_fast_push_front_us\":" << fm_fast_push_front_us
           << ",\"fm_fast_pop_front_us\":"  << fm_fast_pop_front_us
           << ",\"fm_fast_pop_back_us\":"   << fm_fast_pop_back_us
           << ",\"fm_fast_mixed_us\":"      << fm_fast_mixed_us << "}";
    }

    json << "]}";

    string out = json.str();
    ofstream f("tcp_deque_perf_report.json");
    if (f)
      f << out;

    cout << out << endl;
    cout << "Passed" << endl;
  }
};

int argc;
void _main(void *arg) {
  char **argv = static_cast<char **>(arg);
  string ip_addr_port(argv[1]);
  auto raddr = helpers::str_to_netaddr(ip_addr_port);
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, kNumGCThreads,
                                  new TCPDevice(raddr, kNumConnections, kFarMemSize)));
  DequePerfReport report;
  report.do_work(manager.get());
}

int main(int _argc, char *argv[]) {
  if (_argc < 3) {
    cerr << "usage: " << argv[0] << " [cfg_file] [ip_addr:port]" << endl;
    return -EINVAL;
  }

  char conf_path[strlen(argv[1]) + 1];
  strcpy(conf_path, argv[1]);
  for (int i = 2; i < _argc; i++)
    argv[i - 1] = argv[i];
  argc = _argc - 1;

  int ret = runtime_init(conf_path, _main, argv);
  if (ret) {
    cerr << "failed to start runtime" << endl;
    return ret;
  }

  return 0;
}
