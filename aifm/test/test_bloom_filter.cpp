extern "C" {
#include <runtime/runtime.h>
}

#include "bloom_filter.hpp"
#include "helpers.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace far_memory;

void do_work(void *args) {
  std::cout << "Running " << __FILE__ << "..." << std::endl;

  auto filter = BloomFilter::from_expected_count(1000, 0.01);
  TEST_ASSERT(filter.num_bits() > 0);
  TEST_ASSERT(filter.num_hashes() > 0);

  std::vector<std::string> inserted;
  inserted.reserve(1000);
  for (int i = 0; i < 1000; i++) {
    inserted.emplace_back("key_" + std::to_string(i));
    filter.add(inserted.back());
  }

  for (const auto &key : inserted) {
    TEST_ASSERT(filter.possibly_contains(key));
  }

  int false_positives = 0;
  for (int i = 1000; i < 2000; i++) {
    if (filter.possibly_contains("other_" + std::to_string(i))) {
      false_positives++;
    }
  }

  TEST_ASSERT(false_positives < 80);

  filter.clear();
  for (int i = 0; i < 500; i++) {
    TEST_ASSERT(filter.possibly_contains("key_" + std::to_string(i)) == false);
  }

  std::cout << "Passed" << std::endl;
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], do_work, NULL);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
