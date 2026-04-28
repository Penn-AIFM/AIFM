#pragma once

namespace far_memory {

template <typename T>
FORCE_INLINE Deque<T>::Deque(const DerefScope &scope)
    : list_(scope, /* enable_merge = */ false, /* customized_split = */ true) {
}

template <typename T> FORCE_INLINE bool Deque<T>::empty() const {
  return list_.empty();
}

template <typename T> FORCE_INLINE uint64_t Deque<T>::size() const {
  return list_.size();
}

template <typename T>
FORCE_INLINE const T &Deque<T>::cfront(const DerefScope &scope) const {
  return list_.cfront(scope);
}

template <typename T>
FORCE_INLINE const T &Deque<T>::cback(const DerefScope &scope) const {
  return list_.cback(scope);
}

template <typename T>
FORCE_INLINE T &Deque<T>::front(const DerefScope &scope) const {
  return list_.front(scope);
}

template <typename T>
FORCE_INLINE T &Deque<T>::back(const DerefScope &scope) const {
  return list_.back(scope);
}

template <typename T>
FORCE_INLINE void Deque<T>::push_front(const DerefScope &scope,
                                       const T &data) {
  list_.push_front(scope, data);
}

template <typename T>
FORCE_INLINE void Deque<T>::push_back(const DerefScope &scope, const T &data) {
  list_.push_back(scope, data);
}

template <typename T>
FORCE_INLINE void Deque<T>::pop_front(const DerefScope &scope) {
  list_.pop_front(scope);
}

template <typename T>
FORCE_INLINE void Deque<T>::pop_back(const DerefScope &scope) {
  list_.pop_back(scope);
}

} // namespace far_memory
