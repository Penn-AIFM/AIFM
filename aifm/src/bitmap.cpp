#include "bitmap.hpp"

#include "device.hpp"
#include "internal/ds_info.hpp"
#include "manager.hpp"

#include <cstring>

namespace far_memory {

Bitmap::Bitmap(uint8_t ds_id, uint64_t num_bits)
    : ds_id_(ds_id), num_bits_(num_bits),
      device_(FarMemManagerFactory::get()->get_device()) {
  auto params = reinterpret_cast<uint8_t *>(&num_bits_);
  FarMemManagerFactory::get()->construct(kBitmapDSType, ds_id_, sizeof(num_bits_),
                                         params);
}

Bitmap::Bitmap(Bitmap &&other)
    : ds_id_(other.ds_id_), num_bits_(other.num_bits_), device_(other.device_) {
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

void Bitmap::set(uint64_t idx) {
  uint16_t output_len;
  uint8_t output_buf[sizeof(uint8_t)];
  device_->compute(ds_id_, kOpSet, sizeof(idx),
                   reinterpret_cast<const uint8_t *>(&idx), &output_len,
                   output_buf);
}

void Bitmap::clear(uint64_t idx) {
  uint16_t output_len;
  uint8_t output_buf[sizeof(uint8_t)];
  device_->compute(ds_id_, kOpClear, sizeof(idx),
                   reinterpret_cast<const uint8_t *>(&idx), &output_len,
                   output_buf);
}

bool Bitmap::test(uint64_t idx) const {
  uint16_t output_len;
  uint8_t output = 0;
  device_->compute(ds_id_, kOpTest, sizeof(idx),
                   reinterpret_cast<const uint8_t *>(&idx), &output_len,
                   &output);
  return output;
}

void Bitmap::reset() {
  uint16_t output_len;
  uint8_t output_buf[sizeof(uint8_t)];
  device_->compute(ds_id_, kOpReset, 0, nullptr, &output_len, output_buf);
}

uint64_t Bitmap::count() const {
  uint16_t output_len;
  uint64_t output = 0;
  device_->compute(ds_id_, kOpCount, 0, nullptr, &output_len,
                   reinterpret_cast<uint8_t *>(&output));
  return output;
}

} // namespace far_memory
