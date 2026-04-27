extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <random>

using namespace far_memory;

constexpr uint64_t kCacheSize = (256ULL << 20);
constexpr uint64_t kFarMemSize = (8ULL << 30);
constexpr uint32_t kNumGCThreads = 12;
constexpr uint64_t kMaxHeap = 512 * 1024;
constexpr uint32_t kNumOps = 400000;
constexpr uint32_t kScopeResetInterval = 256;

namespace far_memory {

class FarMemTest {
public:
  void do_work(FarMemManager *manager) {
    std::cout << "Running " << __FILE__ "..." << std::endl;
    DerefScope scope;
    Heap<int, kMaxHeap> heap =
        FarMemManagerFactory::get()->allocate_heap<int, kMaxHeap>();

    std::random_device rd;
    std::mt19937_64 eng(rd());
    std::uniform_int_distribution<int> distr;

    for (uint32_t i = 0; i < kNumOps; i++) {
      if (unlikely(i % kScopeResetInterval == 0)) {
        scope.renew();
      }
      heap.push(scope, distr(eng));
    }

    TEST_ASSERT(heap.size() == kNumOps);
    int prev = heap.ctop(scope);
    heap.pop(scope);
    for (uint32_t i = 1; i < kNumOps; i++) {
      if (unlikely(i % kScopeResetInterval == 0)) {
        scope.renew();
      }
      int cur = heap.ctop(scope);
      TEST_ASSERT(prev <= cur);
      prev = cur;
      heap.pop(scope);
    }
    TEST_ASSERT(heap.empty());

    std::cout << "Passed" << std::endl;
  }
};
} // namespace far_memory

void _main(void *arg) {
  std::unique_ptr<FarMemManager> manager =
      std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
          kCacheSize, kNumGCThreads, new FakeDevice(kFarMemSize)));
  FarMemTest test;
  test.do_work(manager.get());
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
