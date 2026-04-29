extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "list.hpp"
#include "manager.hpp"

#include <iostream>
#include <memory>

using namespace far_memory;

struct Data {
  uint32_t data;
  uint32_t dummy[1023];

  Data(uint32_t _data) : data(_data) {}
};

constexpr uint64_t kCacheSize = (256ULL << 20);
constexpr uint64_t kFarMemSize = (8ULL << 30);
constexpr uint32_t kNumGCThreads = 12;
// 4 cases run sequentially with customized_split=true (same as Queue/Stack).
// kNumDataEntries = 2 * kCacheSize / sizeof(Data) / 4 so 4 cases together
// push ~2x the cache, which is enough GC pressure without being excessive.
constexpr uint32_t kNumDataEntries = kCacheSize / (2 * sizeof(Data));
constexpr uint32_t kScopeResetInterval = 256;

namespace far_memory {

class FarMemTest {
public:
  void do_work(FarMemManager *manager) {
    std::cout << "Running " << __FILE__ << "..." << std::endl;

#define DEQUE_PROGRESS(i, label)                                               \
  do {                                                                         \
    if (unlikely((i) % (kNumDataEntries / 4) == 0))                           \
      fprintf(stderr, "  " label " %u%%\n",                                   \
              100u * (i) / kNumDataEntries);                                   \
  } while (0)

    // Case 1: push_back + pop_front  (FIFO)
    {
      DerefScope scope;
      Deque<Data> dq =
          FarMemManagerFactory::get()->allocate_deque<Data>(scope, true);
      fprintf(stderr, "[1/4] push_back + pop_front\n");
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "push_back");
        dq.push_back(scope, Data(i));
      }
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "pop_front");
        TEST_ASSERT(dq.cfront(scope).data == i);
        dq.pop_front(scope);
      }
      fprintf(stderr, "[1/4] done\n");
    }

    // Case 2: push_front + pop_front  (LIFO)
    {
      DerefScope scope;
      Deque<Data> dq =
          FarMemManagerFactory::get()->allocate_deque<Data>(scope, true);
      fprintf(stderr, "[2/4] push_front + pop_front\n");
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "push_front");
        dq.push_front(scope, Data(i));
      }
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "pop_front");
        TEST_ASSERT(dq.cfront(scope).data == kNumDataEntries - 1 - i);
        dq.pop_front(scope);
      }
      fprintf(stderr, "[2/4] done\n");
    }

    // Case 3: push_back + pop_back  (stack from back)
    {
      DerefScope scope;
      Deque<Data> dq =
          FarMemManagerFactory::get()->allocate_deque<Data>(scope, true);
      fprintf(stderr, "[3/4] push_back + pop_back\n");
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "push_back");
        dq.push_back(scope, Data(i));
      }
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "pop_back");
        TEST_ASSERT(dq.cback(scope).data == kNumDataEntries - 1 - i);
        dq.pop_back(scope);
      }
      fprintf(stderr, "[3/4] done\n");
    }

    // Case 4: push_front + pop_back  (reversed FIFO)
    {
      DerefScope scope;
      Deque<Data> dq =
          FarMemManagerFactory::get()->allocate_deque<Data>(scope, true);
      fprintf(stderr, "[4/4] push_front + pop_back\n");
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "push_front");
        dq.push_front(scope, Data(i));
      }
      for (uint32_t i = 0; i < kNumDataEntries; i++) {
        if (unlikely(i % kScopeResetInterval == 0))
          scope.renew();
        DEQUE_PROGRESS(i, "pop_back");
        TEST_ASSERT(dq.cback(scope).data == i);
        dq.pop_back(scope);
      }
      fprintf(stderr, "[4/4] done\n");
    }

#undef DEQUE_PROGRESS

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
