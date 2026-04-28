#pragma once

#include <cassert>

namespace far_memory {

template <typename T, uint64_t MaxN>
FORCE_INLINE Heap<T, MaxN>::Heap(FarMemManager *manager) : array_(manager) {
  // Array's stride prefetcher does not match heap index walks (sift-up/sift-down);
  // disable to avoid add_trace overhead with no benefit.
  array_.disable_prefetch();
}

template <typename T, uint64_t MaxN>
FORCE_INLINE bool Heap<T, MaxN>::empty() const {
  return size_ == 0;
}

template <typename T, uint64_t MaxN>
FORCE_INLINE uint64_t Heap<T, MaxN>::size() const {
  return size_;
}

template <typename T, uint64_t MaxN>
FORCE_INLINE const T &Heap<T, MaxN>::ctop(const DerefScope &scope) {
  assert(!empty());
  return array_.at(scope, 0);
}

template <typename T, uint64_t MaxN>
FORCE_INLINE T &Heap<T, MaxN>::top(const DerefScope &scope) {
  assert(!empty());
  return array_.at_mut(scope, 0);
}

template <typename T, uint64_t MaxN>
FORCE_INLINE void Heap<T, MaxN>::push(const DerefScope &scope, const T &data) {
  assert(size_ < MaxN);
  array_.at_mut(scope, size_) = data;
  sift_up(scope, size_);
  size_++;
}

template <typename T, uint64_t MaxN>
FORCE_INLINE void Heap<T, MaxN>::pop(const DerefScope &scope) {
  assert(!empty());
  if (size_ == 1) {
    size_--;
    return;
  }
  T last = array_.at(scope, size_ - 1);
  array_.at_mut(scope, 0) = last;
  size_--;
  sift_down(scope, 0);
}

template <typename T, uint64_t MaxN>
FORCE_INLINE void Heap<T, MaxN>::sift_up(const DerefScope &scope, uint64_t i) {
  while (i > 0) {
    uint64_t p = parent_of(i);
    T vi = array_.at(scope, i);
    T vp = array_.at(scope, p);
    if (!(vi < vp)) {
      break;
    }
    array_.at_mut(scope, i) = vp;
    array_.at_mut(scope, p) = vi;
    i = p;
  }
}

template <typename T, uint64_t MaxN>
FORCE_INLINE void Heap<T, MaxN>::sift_down(const DerefScope &scope,
                                           uint64_t i) {
  // Future: heap-aware prefetch could call swap_in on grandchildren here.
  while (true) {
    uint64_t l = left_of(i);
    uint64_t r = right_of(i);
    uint64_t smallest = i;
    if (l < size_ && array_.at(scope, l) < array_.at(scope, smallest)) {
      smallest = l;
    }
    if (r < size_ && array_.at(scope, r) < array_.at(scope, smallest)) {
      smallest = r;
    }
    if (smallest == i) {
      break;
    }
    T vi = array_.at(scope, i);
    T vs = array_.at(scope, smallest);
    array_.at_mut(scope, i) = vs;
    array_.at_mut(scope, smallest) = vi;
    i = smallest;
  }
}

} // namespace far_memory
