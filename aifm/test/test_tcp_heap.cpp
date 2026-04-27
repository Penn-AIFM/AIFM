extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>

using namespace far_memory;

constexpr static uint64_t kCacheSize = (256ULL << 20);
constexpr static uint64_t kFarMemSize = (8ULL << 30);
constexpr static uint32_t kNumGCThreads = 12;
constexpr static uint32_t kNumConnections = 300;
constexpr uint64_t kMaxHeap = 512 * 1024;
constexpr uint32_t kNumOps = 400000;
constexpr uint32_t kScopeResetInterval = 256;

void do_work(FarMemManager *manager) {
  std::cout << "Running " << __FILE__ "..." << std::endl;
  DerefScope scope;
  Heap<int, kMaxHeap> heap = manager->allocate_heap<int, kMaxHeap>();

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

int argc;
void _main(void *arg) {
  char **argv = static_cast<char **>(arg);
  std::string ip_addr_port(argv[1]);
  auto raddr = helpers::str_to_netaddr(ip_addr_port);
  std::unique_ptr<FarMemManager> manager =
      std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
          kCacheSize, kNumGCThreads,
          new TCPDevice(raddr, kNumConnections, kFarMemSize)));
  do_work(manager.get());
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

  ret = runtime_init(conf_path, _main, argv);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
