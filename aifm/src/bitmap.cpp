#include "bitmap.hpp"

#include "device.hpp"
#include "internal/ds_info.hpp"
#include "manager.hpp"

#include <array>
#include <atomic>
#include <cstring>
#include <limits>

namespace far_memory {

namespace {
constexpr uint32_t kTestCacheEntries = 4096;
constexpr uint64_t kInvalidTag = std::numeric_limits<uint64_t>::max();

struct TestCacheEntry {
  uint64_t tag = kInvalidTag;
  uint64_t epoch = 0;
  uint64_t word = 0;
};

thread_local std::array<TestCacheEntry, kTestCacheEntries> test_cache;
std::atomic<uint64_t> next_cache_key{1};

inline uint64_t build_cache_tag(uint64_t cache_key, uint64_t word_idx) {
  return (cache_key << 32) ^ word_idx;
}
} // namespace

Bitmap::Bitmap(uint8_t ds_id, uint64_t num_bits)
    : ds_id_(ds_id), num_bits_(num_bits),
      device_(FarMemManagerFactory::get()->get_device()),
      cache_key_(next_cache_key.fetch_add(1, std::memory_order_relaxed)),
      cache_epoch_(0) {
  auto params = reinterpret_cast<uint8_t *>(&num_bits_);
  FarMemManagerFactory::get()->construct(kBitmapDSType, ds_id_, sizeof(num_bits_),
                                         params);
}

Bitmap::Bitmap(Bitmap &&other)
    : ds_id_(other.ds_id_), num_bits_(other.num_bits_), device_(other.device_),
      cache_key_(other.cache_key_),
      cache_epoch_(other.cache_epoch_.load(std::memory_order_relaxed)) {
  other.moved_ = true;
}

Bitmap &Bitmap::operator=(Bitmap &&other) {
  if (this == &other) {
    return *this;
  }
  cleanup();
  ds_id_ = other.ds_id_;
  num_bits_ = other.num_bits_;
  device_ = other.device_;
  cache_key_ = other.cache_key_;
  cache_epoch_.store(other.cache_epoch_.load(std::memory_order_relaxed),
                     std::memory_order_relaxed);
  moved_ = false;
  other.moved_ = true;
  return *this;
}

Bitmap::~Bitmap() { cleanup(); }

void Bitmap::cleanup() {
  if (!moved_) {
    FarMemManagerFactory::get()->destruct(ds_id_);
    moved_ = true;
  }
}

uint64_t Bitmap::size() const { return num_bits_; }

void Bitmap::invalidate_test_cache() {
  cache_epoch_.fetch_add(1, std::memory_order_relaxed);
}

uint64_t Bitmap::fetch_word(uint64_t word_idx) const {
  uint16_t output_len;
  uint64_t output = 0;
  device_->compute(ds_id_, kOpGetWord, sizeof(word_idx),
                   reinterpret_cast<const uint8_t *>(&word_idx), &output_len,
                   reinterpret_cast<uint8_t *>(&output));
  return output;
}

void Bitmap::set(uint64_t idx) {
  uint16_t output_len;
  uint8_t output_buf[sizeof(uint8_t)];
  device_->compute(ds_id_, kOpSet, sizeof(idx),
                   reinterpret_cast<const uint8_t *>(&idx), &output_len,
                   output_buf);
  invalidate_test_cache();
}

void Bitmap::clear(uint64_t idx) {
  uint16_t output_len;
  uint8_t output_buf[sizeof(uint8_t)];
  device_->compute(ds_id_, kOpClear, sizeof(idx),
                   reinterpret_cast<const uint8_t *>(&idx), &output_len,
                   output_buf);
  invalidate_test_cache();
}

bool Bitmap::test(uint64_t idx) const {
  auto word_idx = idx >> 6;
  auto bit_offset = idx & 63;
  auto epoch = cache_epoch_.load(std::memory_order_relaxed);
  auto tag = build_cache_tag(cache_key_, word_idx);
  auto cache_idx =
      (word_idx * 11400714819323198485ULL + cache_key_) & (kTestCacheEntries - 1);
  auto &entry = test_cache[cache_idx];
  uint64_t word;
  if (likely(entry.tag == tag && entry.epoch == epoch)) {
    word = entry.word;
  } else {
    word = fetch_word(word_idx);
    entry.tag = tag;
    entry.epoch = epoch;
    entry.word = word;
  }
  return static_cast<bool>((word >> bit_offset) & 1ULL);
}

void Bitmap::reset() {
  uint16_t output_len;
  uint8_t output_buf[sizeof(uint8_t)];
  device_->compute(ds_id_, kOpReset, 0, nullptr, &output_len, output_buf);
  invalidate_test_cache();
}

uint64_t Bitmap::count() const {
  uint16_t output_len;
  uint64_t output = 0;
  device_->compute(ds_id_, kOpCount, 0, nullptr, &output_len,
                   reinterpret_cast<uint8_t *>(&output));
  return output;
}

} // namespace far_memory
