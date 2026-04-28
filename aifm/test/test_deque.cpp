extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <iostream>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = (1ULL << 28);
constexpr uint64_t kFarMemSize = (512ULL << 20);
constexpr uint32_t kScopeResetInterval = 256;

namespace far_memory {

class FarMemTest {
public:
  void do_work(FarMemManager *manager) {
    cout << "Running " << __FILE__ << "..." << endl;
    DerefScope scope;

    // push_back / pop_front: FIFO queue semantics
    {
      auto dq = manager->allocate_deque<uint32_t>(scope);
      TEST_ASSERT(dq.empty());
      TEST_ASSERT(dq.size() == 0);

      constexpr uint32_t N = 4096;
      for (uint32_t i = 0; i < N; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        dq.push_back(scope, i);
        TEST_ASSERT(dq.size() == i + 1);
      }
      for (uint32_t i = 0; i < N; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        TEST_ASSERT(dq.cfront(scope) == i);
        TEST_ASSERT(dq.front(scope) == i);
        dq.pop_front(scope);
      }
      TEST_ASSERT(dq.empty());
    }

    // push_front / pop_back: reverse FIFO
    {
      auto dq = manager->allocate_deque<uint32_t>(scope);
      constexpr uint32_t N = 4096;
      for (uint32_t i = 0; i < N; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        dq.push_front(scope, i);
      }
      for (uint32_t i = 0; i < N; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        TEST_ASSERT(dq.cback(scope) == i);
        TEST_ASSERT(dq.back(scope) == i);
        dq.pop_back(scope);
      }
      TEST_ASSERT(dq.empty());
    }

    // push_back / pop_back: LIFO stack semantics from back
    {
      auto dq = manager->allocate_deque<uint32_t>(scope);
      constexpr uint32_t N = 4096;
      for (uint32_t i = 0; i < N; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        dq.push_back(scope, i);
      }
      for (uint32_t i = N; i-- > 0;) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        TEST_ASSERT(dq.cback(scope) == i);
        dq.pop_back(scope);
      }
      TEST_ASSERT(dq.empty());
    }

    // push_front / pop_front: LIFO stack semantics from front
    {
      auto dq = manager->allocate_deque<uint32_t>(scope);
      constexpr uint32_t N = 4096;
      for (uint32_t i = 0; i < N; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        dq.push_front(scope, i);
      }
      for (uint32_t i = N; i-- > 0;) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        TEST_ASSERT(dq.cfront(scope) == i);
        dq.pop_front(scope);
      }
      TEST_ASSERT(dq.empty());
    }

    // Mixed: interleaved push_back and push_front, drain both ends
    // After N iterations: front→back = [2N-1, ..., N, 0, 1, ..., N-1]
    {
      auto dq = manager->allocate_deque<uint32_t>(scope);
      constexpr uint32_t N = 512;
      for (uint32_t i = 0; i < N; i++) {
        dq.push_back(scope, i);
        dq.push_front(scope, N + i);
      }
      TEST_ASSERT(dq.size() == 2 * N);
      TEST_ASSERT(dq.cfront(scope) == 2 * N - 1);
      TEST_ASSERT(dq.cback(scope) == N - 1);

      for (uint32_t i = 0; i < N; i++) {
        TEST_ASSERT(dq.cfront(scope) == 2 * N - 1 - i);
        dq.pop_front(scope);
        TEST_ASSERT(dq.cback(scope) == N - 1 - i);
        dq.pop_back(scope);
      }
      TEST_ASSERT(dq.empty());
    }

    // Single-element edge cases
    {
      auto dq = manager->allocate_deque<uint32_t>(scope);
      dq.push_back(scope, 42u);
      TEST_ASSERT(dq.size() == 1);
      TEST_ASSERT(dq.cfront(scope) == 42u);
      TEST_ASSERT(dq.cback(scope) == 42u);
      dq.pop_front(scope);
      TEST_ASSERT(dq.empty());

      dq.push_front(scope, 99u);
      TEST_ASSERT(dq.cfront(scope) == 99u);
      TEST_ASSERT(dq.cback(scope) == 99u);
      dq.pop_back(scope);
      TEST_ASSERT(dq.empty());
    }

    cout << "Passed" << endl;
  }
};

} // namespace far_memory

void _main(void *arg) {
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, std::nullopt,
                                  new FakeDevice(kFarMemSize)));
  FarMemTest test;
  test.do_work(manager.get());
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    cerr << "usage: " << argv[0] << " [cfg_file]" << endl;
    return -EINVAL;
  }
  int ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    cerr << "failed to start runtime" << endl;
    return ret;
  }
  return 0;
}
