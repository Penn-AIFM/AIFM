#pragma once

#include "array.hpp"
#include "deref_scope.hpp"

namespace far_memory {

class FarMemManager;

template <typename T, uint64_t MaxN> class Heap {
private:
  Heap(FarMemManager *manager);

  Array<T, MaxN> array_;
  uint64_t size_ = 0;
  friend class FarMemManager;

  static constexpr uint64_t parent_of(uint64_t i) { return (i - 1) / 2; }
  static constexpr uint64_t left_of(uint64_t i) { return 2 * i + 1; }
  static constexpr uint64_t right_of(uint64_t i) { return 2 * i + 2; }
  void sift_up(const DerefScope &scope, uint64_t i);
  void sift_down(const DerefScope &scope, uint64_t i);

public:
  bool empty() const;
  uint64_t size() const;
  static constexpr uint64_t capacity() { return MaxN; }
  const T &ctop(const DerefScope &scope);
  T &top(const DerefScope &scope);
  void push(const DerefScope &scope, const T &data);
  void pop(const DerefScope &scope);
};

} // namespace far_memory

#include "internal/heap.ipp"
