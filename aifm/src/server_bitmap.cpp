#include "server_bitmap.hpp"

extern "C" {
#include <base/assert.h>
}

#include <algorithm>

namespace far_memory {

ServerBitmap::ServerBitmap(uint32_t param_len, uint8_t *params) {
  BUG_ON(param_len != sizeof(num_bits_));
  num_bits_ = *reinterpret_cast<uint64_t *>(params);
  auto num_words = (num_bits_ + 63) / 64;
  words_.resize(num_words, 0);
}

ServerBitmap::~ServerBitmap() {}

void ServerBitmap::read_object(uint8_t obj_id_len, const uint8_t *obj_id,
                               uint16_t *data_len, uint8_t *data_buf) {
  BUG();
}

void ServerBitmap::write_object(uint8_t obj_id_len, const uint8_t *obj_id,
                                uint16_t data_len, const uint8_t *data_buf) {
  BUG();
}

bool ServerBitmap::remove_object(uint8_t obj_id_len, const uint8_t *obj_id) {
  BUG();
}

void ServerBitmap::compute(uint8_t opcode, uint16_t input_len,
                           const uint8_t *input_buf, uint16_t *output_len,
                           uint8_t *output_buf) {
  switch (opcode) {
  case kOpSet: {
    auto writer_lock = lock_.get_writer_lock();
    BUG_ON(input_len != sizeof(uint64_t));
    auto idx = *reinterpret_cast<const uint64_t *>(input_buf);
    BUG_ON(idx >= num_bits_);
    words_[idx / 64] |= (1ULL << (idx % 64));
    *output_len = 0;
    return;
  }
  case kOpClear: {
    auto writer_lock = lock_.get_writer_lock();
    BUG_ON(input_len != sizeof(uint64_t));
    auto idx = *reinterpret_cast<const uint64_t *>(input_buf);
    BUG_ON(idx >= num_bits_);
    words_[idx / 64] &= ~(1ULL << (idx % 64));
    *output_len = 0;
    return;
  }
  case kOpTest: {
    auto reader_lock = lock_.get_reader_lock();
    BUG_ON(input_len != sizeof(uint64_t));
    auto idx = *reinterpret_cast<const uint64_t *>(input_buf);
    BUG_ON(idx >= num_bits_);
    *output_len = sizeof(uint8_t);
    *output_buf = ((words_[idx / 64] >> (idx % 64)) & 1ULL);
    return;
  }
  case kOpReset: {
    auto writer_lock = lock_.get_writer_lock();
    BUG_ON(input_len != 0);
    std::fill(words_.begin(), words_.end(), 0);
    *output_len = 0;
    return;
  }
  case kOpCount: {
    auto reader_lock = lock_.get_reader_lock();
    BUG_ON(input_len != 0);
    uint64_t total = 0;
    for (auto word : words_) {
      total += static_cast<uint64_t>(__builtin_popcountll(word));
    }
    *output_len = sizeof(total);
    *reinterpret_cast<uint64_t *>(output_buf) = total;
    return;
  }
  default:
    BUG();
  }
}

ServerDS *ServerBitmapFactory::build(uint32_t param_len, uint8_t *params) {
  return new ServerBitmap(param_len, params);
}

} // namespace far_memory
