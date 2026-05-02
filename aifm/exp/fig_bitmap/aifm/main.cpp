extern "C" {
#include <runtime/runtime.h>
}
#include "thread.h"

#include "bitmap.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"
#include "zipf.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

using namespace far_memory;
using namespace std;

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

std::atomic_flag flag;
std::unique_ptr<std::mt19937> generators[helpers::kNumCPUs];
__thread uint32_t per_core_req_idx = 0;
std::unique_ptr<FarMemManager> manager;

namespace far_memory {
class FarMemTest {
private:
  constexpr static uint64_t kCacheSize = 2048 * Region::kSize;
  constexpr static uint64_t kFarMemSize = (1ULL << 30);
  constexpr static uint32_t kNumGCThreads = 4;
  constexpr static uint64_t kNumBits = 1ULL << 20;
  constexpr static uint64_t kNumSeedBits = 1ULL << 14;
  constexpr static uint64_t kBitmapStep = 1315423911;
  constexpr static uint32_t kNumMutatorThreads = 16;
  constexpr static uint32_t kNumItersPerScope = 64;
  constexpr static uint32_t kReqSeqLenPerCore = 1 << 16;
  constexpr static uint32_t kNumConnections = 256;
  constexpr static uint32_t kMonitorPerIter = 16384;
  constexpr static uint32_t kMinMonitorIntervalUs = 2 * 1000 * 1000;
  constexpr static uint32_t kMaxRunningUs = 20 * 1000 * 1000;
  constexpr static double kZipfParamS = 1.35;

  struct alignas(64) Cnt {
    uint64_t c;
  };

  uint32_t all_zipf_indices[helpers::kNumCPUs][kReqSeqLenPerCore];
  Cnt cnts[kNumMutatorThreads];
  Cnt hit_cnts[kNumMutatorThreads];
  std::vector<double> mops_vec;
  std::vector<double> hit_rate_vec;

  uint64_t prev_sum_cnts = 0;
  uint64_t prev_sum_hits = 0;
  uint64_t prev_us = 0;
  uint64_t running_us = 0;

  template <typename T> T avg_of_last_n(std::vector<T> &vec, uint32_t n) {
    std::vector<T> last_n(
        vec.end() - std::min(static_cast<uint32_t>(vec.size()), n), vec.end());
    return std::accumulate(last_n.begin(), last_n.end(), 0.0) / last_n.size();
  }

  void prepare(Bitmap *bitmap_ptr) {
    for (uint32_t i = 0; i < helpers::kNumCPUs; i++) {
      std::random_device rd;
      generators[i].reset(new std::mt19937(rd()));
    }

    bitmap_ptr->reset();
    for (uint64_t i = 0; i < kNumSeedBits; i++) {
      auto idx = (i * kBitmapStep) & (kNumBits - 1);
      bitmap_ptr->set(idx);
    }

    preempt_disable();
    zipf_table_distribution<> zipf(kNumBits, kZipfParamS);
    auto &generator = generators[get_core_num()];
    for (uint32_t i = 0; i < kReqSeqLenPerCore; i++) {
      auto idx = zipf(*generator);
      BUG_ON(idx >= kNumBits);
      all_zipf_indices[0][i] = idx;
    }
    for (uint32_t k = 1; k < helpers::kNumCPUs; k++) {
      memcpy(all_zipf_indices[k], all_zipf_indices[0],
             sizeof(uint32_t) * kReqSeqLenPerCore);
    }
    preempt_enable();
  }

  void monitor_perf() {
    if (!flag.test_and_set()) {
      auto us = microtime();
      if (us - prev_us > kMinMonitorIntervalUs) {
        uint64_t sum_cnts = 0;
        uint64_t sum_hits = 0;
        for (uint32_t i = 0; i < kNumMutatorThreads; i++) {
          sum_cnts += ACCESS_ONCE(cnts[i].c);
          sum_hits += ACCESS_ONCE(hit_cnts[i].c);
        }
        us = microtime();
        auto delta_cnts = sum_cnts - prev_sum_cnts;
        auto mops = static_cast<double>(delta_cnts) / (us - prev_us);
        auto hit_rate =
            delta_cnts ? static_cast<double>(sum_hits - prev_sum_hits) / delta_cnts
                       : 0.0;
        mops_vec.push_back(mops);
        hit_rate_vec.push_back(hit_rate);
        running_us += (us - prev_us);
        if (running_us >= kMaxRunningUs) {
          std::fprintf(stdout, "mops = %.6f, hit_rate = %.6f\n",
                       avg_of_last_n(mops_vec, 5), avg_of_last_n(hit_rate_vec, 5));
          std::fprintf(stdout, "Done. Force exiting...\n");
          std::fflush(stdout);
          _Exit(0);
        }
        prev_us = us;
        prev_sum_cnts = sum_cnts;
        prev_sum_hits = sum_hits;
      }
      flag.clear();
    }
  }

  void bench_test(Bitmap *bitmap_ptr) {
    prev_us = microtime();
    std::vector<rt::Thread> threads;
    for (uint32_t tid = 0; tid < kNumMutatorThreads; tid++) {
      threads.emplace_back(rt::Thread([&, tid]() {
        uint32_t cnt = 0;
        while (1) {
          if (unlikely(cnt++ % kNumItersPerScope == 0)) {
            // Keep parity with fig9 structure; bitmap::test does not need scope.
          }
          preempt_disable();
          if (unlikely(cnt % kMonitorPerIter == 0)) {
            monitor_perf();
          }
          auto idx = all_zipf_indices[get_core_num()][per_core_req_idx++];
          if (unlikely(per_core_req_idx == kReqSeqLenPerCore)) {
            per_core_req_idx = 0;
          }
          preempt_enable();
          auto hit = bitmap_ptr->test(idx);
          ACCESS_ONCE(cnts[tid].c)++;
          ACCESS_ONCE(hit_cnts[tid].c) += hit;
        }
      }));
    }
    for (auto &thread : threads) {
      thread.Join();
    }
  }

public:
  void run(netaddr raddr) {
    manager.reset(FarMemManagerFactory::build(
        kCacheSize, kNumGCThreads,
        new TCPDevice(raddr, kNumConnections, kFarMemSize)));
    auto bitmap = manager->allocate_bitmap(kNumBits);
    std::cout << "Prepare..." << std::endl;
    prepare(&bitmap);
    std::cout << "Test..." << std::endl;
    bench_test(&bitmap);
    manager.reset();
  }
};
} // namespace far_memory

int argc;
FarMemTest test;
void my_main(void *arg) {
  char **argv = reinterpret_cast<char **>(arg);
  std::string ip_addr_port(argv[1]);
  test.run(helpers::str_to_netaddr(ip_addr_port));
}

int main(int _argc, char *argv[]) {
  int ret;

  if (_argc < 3) {
    std::cerr << "usage: [cfg_file] [ip_addr:port]" << std::endl;
    return -EINVAL;
  }

  char conf_path[strlen(argv[1]) + 1];
  strcpy(conf_path, argv[1]);
  for (int i = 2; i < _argc; i++) {
    argv[i - 1] = argv[i];
  }
  argc = _argc - 1;

  ret = runtime_init(conf_path, my_main, argv);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
