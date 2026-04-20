#pragma once

#include "reader_writer_lock.hpp"
#include "server.hpp"

#include <cstdint>
#include <vector>

namespace far_memory {

class ServerBitmap : public ServerDS {
private:
  enum OpCode : uint8_t {
    kOpSet = 0,
    kOpClear = 1,
    kOpTest = 2,
    kOpReset = 3,
    kOpCount = 4,
  };

  ReaderWriterLock lock_;
  uint64_t num_bits_;
  std::vector<uint64_t> words_;

  friend class ServerBitmapFactory;

public:
  ServerBitmap(uint32_t param_len, uint8_t *params);
  ~ServerBitmap();

  void read_object(uint8_t obj_id_len, const uint8_t *obj_id, uint16_t *data_len,
                   uint8_t *data_buf);
  void write_object(uint8_t obj_id_len, const uint8_t *obj_id, uint16_t data_len,
                    const uint8_t *data_buf);
  bool remove_object(uint8_t obj_id_len, const uint8_t *obj_id);
  void compute(uint8_t opcode, uint16_t input_len, const uint8_t *input_buf,
               uint16_t *output_len, uint8_t *output_buf);
};

class ServerBitmapFactory : public ServerDSFactory {
public:
  ServerDS *build(uint32_t param_len, uint8_t *params);
};

} // namespace far_memory
