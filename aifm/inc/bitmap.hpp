#pragma once

#include "helpers.hpp"

#include <cstdint>

namespace far_memory {

class FarMemDevice;

class Bitmap {
private:
  enum OpCode : uint8_t {
    kOpSet = 0,
    kOpClear = 1,
    kOpTest = 2,
    kOpReset = 3,
    kOpCount = 4,
  };

  uint8_t ds_id_;
  uint64_t num_bits_;
  FarMemDevice *device_;
  bool moved_ = false;

  Bitmap(uint8_t ds_id, uint64_t num_bits);
  void cleanup();

  friend class FarMemManager;

public:
  NOT_COPYABLE(Bitmap);
  Bitmap(Bitmap &&other);
  Bitmap &operator=(Bitmap &&other);
  ~Bitmap();

  uint64_t size() const;
  void set(uint64_t idx);
  void clear(uint64_t idx);
  bool test(uint64_t idx) const;
  void reset();
  uint64_t count() const;
};

} // namespace far_memory
