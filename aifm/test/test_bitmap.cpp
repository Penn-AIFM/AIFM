extern "C" {
#include <runtime/runtime.h>
}

#include "bitmap.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <iostream>
#include <memory>
#include <vector>

using namespace far_memory;

constexpr static uint64_t kCacheSize = 128 * Region::kSize;
constexpr static uint64_t kFarMemSize = 1ULL << 30;
constexpr static uint32_t kNumGCThreads = 8;
constexpr static uint64_t kNumBits = 1ULL << 20;

void do_work(FarMemManager *manager) {
  std::cout << "Running " << __FILE__ << "..." << std::endl;

  auto bitmap = manager->allocate_bitmap(kNumBits);
  TEST_ASSERT(bitmap.size() == kNumBits);
  TEST_ASSERT(bitmap.count() == 0);

  std::vector<uint64_t> indices = {0, 1, 63, 64, 65, 1024, 1ULL << 18,
                                   kNumBits - 1};
  for (auto idx : indices) {
    bitmap.set(idx);
  }

  for (auto idx : indices) {
    TEST_ASSERT(bitmap.test(idx));
  }

  TEST_ASSERT(bitmap.count() == indices.size());

  bitmap.clear(64);
  TEST_ASSERT(!bitmap.test(64));
  TEST_ASSERT(bitmap.count() == indices.size() - 1);

  bitmap.reset();
  TEST_ASSERT(bitmap.count() == 0);
  for (auto idx : indices) {
    TEST_ASSERT(!bitmap.test(idx));
  }

  std::cout << "Passed" << std::endl;
}

void _main(void *arg) {
  auto manager = std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
      kCacheSize, kNumGCThreads, new FakeDevice(kFarMemSize)));
  do_work(manager.get());
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  int ret = runtime_init(argv[1], _main, nullptr);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
