#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

namespace far_memory {

class BloomFilter {
public:
  BloomFilter(uint64_t num_bits, uint32_t num_hashes);

  static BloomFilter from_expected_count(uint64_t expected_items,
                                         double false_positive_rate);

  void add(const void *data, uint32_t len);
  bool possibly_contains(const void *data, uint32_t len) const;

  void add(std::string_view key);
  bool possibly_contains(std::string_view key) const;

  void clear();

  uint64_t num_bits() const;
  uint32_t num_hashes() const;

private:
  uint64_t num_bits_;
  uint32_t num_hashes_;
  std::vector<uint64_t> words_;
};

} // namespace far_memory

#include "internal/bloom_filter.ipp"
