extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "manager.hpp"

#include <iostream>

using namespace far_memory;

constexpr uint64_t kCacheSize  = (128ULL << 20);
constexpr uint64_t kFarMemSize = (1ULL  << 30);
constexpr uint32_t kNumGCThreads = 4;
constexpr uint32_t N = 20;

namespace far_memory {

class FarMemTest {
public:
  void do_work(FarMemManager *manager) {
    std::cout << "Running " << __FILE__ << "..." << std::endl;

    DerefScope scope;

    // Case 1: push_back + pop_front (FIFO)
    {
      auto dq = FarMemManagerFactory::get()->allocate_deque<uint32_t>(scope);
      for (uint32_t i = 0; i < N; i++)
        dq.push_back(scope, i);
      for (uint32_t i = 0; i < N; i++) {
        TEST_ASSERT(dq.cfront(scope) == i);
        dq.pop_front(scope);
      }
      TEST_ASSERT(dq.empty());
      std::cout << "  case 1 (push_back + pop_front) OK" << std::endl;
    }

    // Case 2: push_front + pop_front (LIFO)
    {
      auto dq = FarMemManagerFactory::get()->allocate_deque<uint32_t>(scope);
      for (uint32_t i = 0; i < N; i++)
        dq.push_front(scope, i);
      for (uint32_t i = 0; i < N; i++) {
        TEST_ASSERT(dq.cfront(scope) == N - 1 - i);
        dq.pop_front(scope);
      }
      TEST_ASSERT(dq.empty());
      std::cout << "  case 2 (push_front + pop_front) OK" << std::endl;
    }

    // Case 3: push_back + pop_back (stack from back)
    {
      auto dq = FarMemManagerFactory::get()->allocate_deque<uint32_t>(scope);
      for (uint32_t i = 0; i < N; i++)
        dq.push_back(scope, i);
      for (uint32_t i = 0; i < N; i++) {
        TEST_ASSERT(dq.cback(scope) == N - 1 - i);
        dq.pop_back(scope);
      }
      TEST_ASSERT(dq.empty());
      std::cout << "  case 3 (push_back + pop_back) OK" << std::endl;
    }

    // Case 4: push_front + pop_back (reversed FIFO)
    {
      auto dq = FarMemManagerFactory::get()->allocate_deque<uint32_t>(scope);
      for (uint32_t i = 0; i < N; i++)
        dq.push_front(scope, i);
      for (uint32_t i = 0; i < N; i++) {
        TEST_ASSERT(dq.cback(scope) == i);
        dq.pop_back(scope);
      }
      TEST_ASSERT(dq.empty());
      std::cout << "  case 4 (push_front + pop_back) OK" << std::endl;
    }

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
  if (argc < 2) {
    std::cerr << "usage: " << argv[0] << " [cfg_file]" << std::endl;
    return -EINVAL;
  }
  int ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }
  return 0;
}
