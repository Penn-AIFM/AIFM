#pragma once

#include "hash.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace far_memory {

inline BloomFilter::BloomFilter(uint64_t num_bits, uint32_t num_hashes)
    : num_bits_(std::max<uint64_t>(1, num_bits)),
      num_hashes_(std::max<uint32_t>(1, num_hashes)),
      words_((num_bits_ + 63) / 64, 0) {}

inline BloomFilter BloomFilter::from_expected_count(uint64_t expected_items,
                                                    double false_positive_rate) {
  if (expected_items == 0) {
    throw std::invalid_argument("expected_items must be > 0");
  }
  if (!(false_positive_rate > 0.0 && false_positive_rate < 1.0)) {
    throw std::invalid_argument(
        "false_positive_rate must be in (0.0, 1.0)");
  }

  constexpr double kLn2 = 0.69314718055994530942;
  const auto m = static_cast<uint64_t>(std::ceil(
      -static_cast<double>(expected_items) * std::log(false_positive_rate) /
      (kLn2 * kLn2)));
  const auto k = static_cast<uint32_t>(
      std::max<double>(1.0, std::round((static_cast<double>(m) /
                                        static_cast<double>(expected_items)) *
                                       kLn2)));
  return BloomFilter(m, k);
}

inline void BloomFilter::add(const void *data, uint32_t len) {
  auto h1 = hash_32(data, len);
  auto h2 = hash_32(&h1, sizeof(h1));
  if (h2 == 0) {
    h2 = 0x9E3779B9U;
  }

  for (uint32_t i = 0; i < num_hashes_; i++) {
    const auto bit_pos = (static_cast<uint64_t>(h1) +
                          static_cast<uint64_t>(i) * static_cast<uint64_t>(h2) +
                          static_cast<uint64_t>(i) * static_cast<uint64_t>(i)) %
                         num_bits_;
    words_[bit_pos >> 6] |= (1ULL << (bit_pos & 63));
  }
}

inline bool BloomFilter::possibly_contains(const void *data, uint32_t len) const {
  auto h1 = hash_32(data, len);
  auto h2 = hash_32(&h1, sizeof(h1));
  if (h2 == 0) {
    h2 = 0x9E3779B9U;
  }

  for (uint32_t i = 0; i < num_hashes_; i++) {
    const auto bit_pos = (static_cast<uint64_t>(h1) +
                          static_cast<uint64_t>(i) * static_cast<uint64_t>(h2) +
                          static_cast<uint64_t>(i) * static_cast<uint64_t>(i)) %
                         num_bits_;
    if ((words_[bit_pos >> 6] & (1ULL << (bit_pos & 63))) == 0) {
      return false;
    }
  }
  return true;
}

inline void BloomFilter::add(std::string_view key) {
  add(key.data(), static_cast<uint32_t>(key.size()));
}

inline bool BloomFilter::possibly_contains(std::string_view key) const {
  return possibly_contains(key.data(), static_cast<uint32_t>(key.size()));
}

inline void BloomFilter::clear() { std::fill(words_.begin(), words_.end(), 0); }

inline uint64_t BloomFilter::num_bits() const { return num_bits_; }

inline uint32_t BloomFilter::num_hashes() const { return num_hashes_; }

} // namespace far_memory
