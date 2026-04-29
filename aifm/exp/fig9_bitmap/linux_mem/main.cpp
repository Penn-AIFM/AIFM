extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
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
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <numeric>
#include <random>
#include <vector>

using namespace far_memory;

#define ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

std::atomic_flag flag;
std::unique_ptr<std::mt19937> generators[helpers::kNumCPUs];
__thread uint32_t per_core_req_idx = 0;

namespace far_memory {
class FarMemTest {
private:
  constexpr static uint64_t kCacheSize = 16384 * Region::kSize;
  constexpr static uint64_t kFarMemSize = (1ULL << 30);
  constexpr static uint32_t kNumGCThreads = 100;
  constexpr static uint64_t kNumBits = 1ULL << 27;
  constexpr static uint64_t kNumSeedBits = 1ULL << 21;
  constexpr static uint64_t kBitmapStep = 1315423911;
  constexpr static uint32_t kNumMutatorThreads = 200;
  constexpr static uint32_t kReqSeqLenPerCore = 1 << 20;
  constexpr static uint32_t kMonitorPerIter = 262144;
  constexpr static uint32_t kMinMonitorIntervalUs = 10 * 1000 * 1000;
  constexpr static uint32_t kMaxRunningUs = 200 * 1000 * 1000; // 200 seconds
  constexpr static double kZipfParamS = 1.15;

  struct alignas(64) Cnt {
    uint64_t c;
  };

  uint32_t all_zipf_bit_indices[helpers::kNumCPUs][kReqSeqLenPerCore];
  Cnt cnts[kNumMutatorThreads];
  std::vector<double> mops_vec;
  uint64_t prev_sum_cnts = 0;
  uint64_t prev_us = 0;
  uint64_t running_us = 0;

  void prepare(Bitmap &bitmap) {
    for (uint32_t i = 0; i < helpers::kNumCPUs; i++) {
      std::random_device rd;
      generators[i].reset(new std::mt19937(rd()));
    }

    bitmap.reset();
    for (uint64_t i = 0; i < kNumSeedBits; i++) {
      auto idx = (i * kBitmapStep) & (kNumBits - 1);
      bitmap.set(idx);
    }

    preempt_disable();
    zipf_table_distribution<> zipf(kNumBits, kZipfParamS);
    auto &generator = generators[get_core_num()];
    for (uint32_t i = 0; i < kReqSeqLenPerCore; i++) {
      auto idx = zipf(*generator);
      BUG_ON(idx >= kNumBits);
      all_zipf_bit_indices[0][i] = idx;
    }
    for (uint32_t k = 1; k < helpers::kNumCPUs; k++) {
      memcpy(all_zipf_bit_indices[k], all_zipf_bit_indices[0],
             sizeof(uint32_t) * kReqSeqLenPerCore);
    }
    preempt_enable();
  }

  void monitor_perf() {
    if (!flag.test_and_set()) {
      auto us = microtime();
      if (us - prev_us > kMinMonitorIntervalUs) {
        uint64_t sum_cnts = 0;
        for (uint32_t i = 0; i < kNumMutatorThreads; i++) {
          sum_cnts += ACCESS_ONCE(cnts[i].c);
        }
        us = microtime();
        auto mops = static_cast<double>(sum_cnts - prev_sum_cnts) /
                    static_cast<double>(us - prev_us);
        mops_vec.push_back(mops);
        running_us += (us - prev_us);
        if (running_us >= kMaxRunningUs) {
          auto n = std::min<size_t>(mops_vec.size(), 5);
          auto avg = std::accumulate(mops_vec.end() - n, mops_vec.end(), 0.0) /
                     static_cast<double>(n);
          std::cout << "mops = " << avg << std::endl;
          std::cout << "Done. Force exiting..." << std::endl;
          exit(0);
        }
        prev_us = us;
        prev_sum_cnts = sum_cnts;
      }
      flag.clear();
    }
  }

  void bench_test(Bitmap &bitmap) {
    prev_us = microtime();
    std::vector<rt::Thread> threads;
    for (uint32_t tid = 0; tid < kNumMutatorThreads; tid++) {
      threads.emplace_back(rt::Thread([&, tid]() {
        uint32_t cnt = 0;
        uint64_t true_hits = 0;
        while (1) {
          if (unlikely(cnt++ % kMonitorPerIter == 0)) {
            monitor_perf();
          }
          preempt_disable();
          auto bit_idx =
              all_zipf_bit_indices[get_core_num()][per_core_req_idx++];
          if (unlikely(per_core_req_idx == kReqSeqLenPerCore)) {
            per_core_req_idx = 0;
          }
          preempt_enable();
          true_hits += bitmap.test(bit_idx);
          ACCESS_ONCE(cnts[tid].c)++;
        }
        std::cout << true_hits << std::endl;
      }));
    }
    for (auto &thread : threads) {
      thread.Join();
    }
  }

public:
  void run() {
    memset(cnts, 0, sizeof(cnts));
    flag.clear();
    auto manager = std::unique_ptr<FarMemManager>(
        FarMemManagerFactory::build(kCacheSize, kNumGCThreads,
                                    new FakeDevice(kFarMemSize)));
    auto bitmap = manager->allocate_bitmap(kNumBits);
    std::cout << "Prepare..." << std::endl;
    prepare(bitmap);
    std::cout << "Test..." << std::endl;
    bench_test(bitmap);
    manager.reset();
  }
};
} // namespace far_memory

FarMemTest test;
void my_main(void *argv) {
  test.run();
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], my_main, NULL);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
