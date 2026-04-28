#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
#include <utility>
#include <vector>

namespace far_memory {

namespace {

struct TreeReadLock {
  std::shared_lock<std::shared_mutex> lock_;
  TreeReadLock(std::shared_mutex &m, bool enable) : lock_(m, std::defer_lock) {
    if (enable) {
      lock_.lock();
    }
  }
};

struct TreeWriteLock {
  std::unique_lock<std::shared_mutex> lock_;
  TreeWriteLock(std::shared_mutex &m, bool enable) : lock_(m, std::defer_lock) {
    if (enable) {
      lock_.lock();
    }
  }
};

// Software prefetch: ptr_addr is the in-memory address of a GenericUniquePtr
// (same encoding as page_deref). Warming the handle before the next deref() can
// hide cache latency. No-op for null. Define AIFM_TREESET_NO_PREFETCH to
// disable (e.g. A/B on odd architectures).
#if (defined(__GNUC__) || defined(__clang__)) && !defined(AIFM_TREESET_NO_PREFETCH)
#define AIFM_TREESET_HAS_PREFETCH 1
#endif

#if AIFM_TREESET_HAS_PREFETCH
inline void prefetch_unique_ptr_handle(uint64_t ptr_addr) {
  if (ptr_addr == 0) {
    return;
  }
  const void *p =
      reinterpret_cast<const void *>(static_cast<uintptr_t>(ptr_addr));
  __builtin_prefetch(p, 0, 3);
}
#else
inline void prefetch_unique_ptr_handle(uint64_t) {}
#endif

} // namespace

// -----------------------------------------------------------------------------
// Iterator
// -----------------------------------------------------------------------------

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse>::GenericIteratorImpl()
    : leaf_addr_(kNull), key_idx_(-1), tree_(nullptr) {}

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse>::GenericIteratorImpl(
    uint64_t leaf, int32_t idx, GenericTreeSet *t)
    : leaf_addr_(leaf), key_idx_(idx), tree_(t) {}

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse>::GenericIteratorImpl(
    const GenericIteratorImpl &o)
    : leaf_addr_(o.leaf_addr_), key_idx_(o.key_idx_), tree_(o.tree_) {}

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse> &
GenericTreeSet::GenericIteratorImpl<Reverse>::operator=(
    const GenericIteratorImpl &o) {
  leaf_addr_ = o.leaf_addr_;
  key_idx_ = o.key_idx_;
  tree_ = o.tree_;
  return *this;
}

template <bool Reverse>
FORCE_INLINE void GenericTreeSet::GenericIteratorImpl<Reverse>::inc(
    const DerefScope &scope) {
  if (leaf_addr_ == kNull || key_idx_ < 0) {
    return;
  }
  if constexpr (!Reverse) {
    uint8_t *base = const_cast<uint8_t *>(
        tree_->page_deref(scope, leaf_addr_));
    const BPlusPageHdr *h = tree_->hdr(base);
    if (key_idx_ + 1 < static_cast<int32_t>(h->num_keys)) {
      key_idx_++;
      return;
    }
    uint64_t nxt = h->next_leaf;
    if (nxt == kNull) {
      leaf_addr_ = kNull;
      key_idx_ = -1;
    } else {
      prefetch_unique_ptr_handle(nxt);
      leaf_addr_ = nxt;
      key_idx_ = 0;
    }
  } else {
    if (key_idx_ > 0) {
      key_idx_--;
      return;
    }
    uint8_t *base = const_cast<uint8_t *>(
        tree_->page_deref(scope, leaf_addr_));
    const BPlusPageHdr *h = tree_->hdr(base);
    uint64_t prv = h->prev_leaf;
    if (prv == kNull) {
      leaf_addr_ = kNull;
      key_idx_ = -1;
    } else {
      prefetch_unique_ptr_handle(prv);
      leaf_addr_ = prv;
      uint8_t *pb =
          const_cast<uint8_t *>(tree_->page_deref(scope, leaf_addr_));
      key_idx_ = static_cast<int32_t>(tree_->hdr(pb)->num_keys) - 1;
    }
  }
}

template <bool Reverse>
FORCE_INLINE void GenericTreeSet::GenericIteratorImpl<Reverse>::dec(
    const DerefScope &scope) {
  if constexpr (!Reverse) {
    if (leaf_addr_ == kNull && key_idx_ < 0) {
      return;
    }
    if (leaf_addr_ != kNull && key_idx_ > 0) {
      key_idx_--;
      return;
    }
    if (leaf_addr_ != kNull && key_idx_ == 0) {
      uint8_t *base = const_cast<uint8_t *>(
          tree_->page_deref(scope, leaf_addr_));
      uint64_t prv = tree_->hdr(base)->prev_leaf;
      if (prv == kNull) {
        return;
      }
      prefetch_unique_ptr_handle(prv);
      leaf_addr_ = prv;
      base = const_cast<uint8_t *>(tree_->page_deref(scope, leaf_addr_));
      key_idx_ = static_cast<int32_t>(tree_->hdr(base)->num_keys) - 1;
    }
  } else {
    uint8_t *base = const_cast<uint8_t *>(
        tree_->page_deref(scope, leaf_addr_));
    const BPlusPageHdr *h = tree_->hdr(base);
    if (key_idx_ + 1 < static_cast<int32_t>(h->num_keys)) {
      key_idx_++;
      return;
    }
    uint64_t nxt = h->next_leaf;
    if (nxt == kNull) {
      leaf_addr_ = kNull;
      key_idx_ = -1;
    } else {
      prefetch_unique_ptr_handle(nxt);
      leaf_addr_ = nxt;
      key_idx_ = 0;
    }
  }
}

template <bool Reverse>
FORCE_INLINE bool GenericTreeSet::GenericIteratorImpl<Reverse>::operator==(
    const GenericIteratorImpl &o) const {
  return leaf_addr_ == o.leaf_addr_ && key_idx_ == o.key_idx_;
}

template <bool Reverse>
FORCE_INLINE bool GenericTreeSet::GenericIteratorImpl<Reverse>::operator!=(
    const GenericIteratorImpl &o) const {
  return !(*this == o);
}

template <bool Reverse>
FORCE_INLINE const uint8_t *
GenericTreeSet::GenericIteratorImpl<Reverse>::deref(const DerefScope &scope) const {
  if (leaf_addr_ == kNull || key_idx_ < 0) {
    return nullptr;
  }
  const uint8_t *base = tree_->page_deref(scope, leaf_addr_);
  return tree_->leaf_key_ptr(base, static_cast<uint16_t>(key_idx_));
}

template <bool Reverse>
FORCE_INLINE uint8_t *
GenericTreeSet::GenericIteratorImpl<Reverse>::deref_mut(const DerefScope &scope) {
  if (leaf_addr_ == kNull || key_idx_ < 0) {
    return nullptr;
  }
  uint8_t *base = tree_->page_deref_mut(scope, leaf_addr_);
  return tree_->leaf_key_ptr(base, static_cast<uint16_t>(key_idx_));
}

template <bool Reverse>
FORCE_INLINE bool
GenericTreeSet::GenericIteratorImpl<Reverse>::is_end() const {
  return leaf_addr_ == kNull || key_idx_ < 0;
}

// -----------------------------------------------------------------------------
// GenericTreeSet basics
// -----------------------------------------------------------------------------

FORCE_INLINE GenericTreeSet::GenericTreeSet(uint16_t item_size, uint8_t ds_id)
    : item_size_(item_size), leaf_cap_(2), internal_cap_(2), min_leaf_keys_(1),
      root_addr_(kNull), size_(0), ds_id_(ds_id) {
  size_t maxd = Object::kMaxObjectDataSize;
  if (sizeof(BPlusPageHdr) >= maxd) {
    assert(false && "item too large for B+ tree page");
  }
  size_t leaf_fit =
      (maxd - sizeof(BPlusPageHdr)) / static_cast<size_t>(item_size_);
  if (leaf_fit < 2) {
    leaf_fit = 2;
  }
  leaf_cap_ = static_cast<uint16_t>(
      std::min(leaf_fit, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));

  if (maxd >= sizeof(BPlusPageHdr) + 8) {
    size_t room = maxd - sizeof(BPlusPageHdr) - 8;
    size_t denom = static_cast<size_t>(item_size_) + 8u;
    size_t int_fit = denom ? room / denom : 0;
    if (int_fit < 2) {
      int_fit = 2;
    }
    internal_cap_ = static_cast<uint16_t>(
        std::min(int_fit, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
  }
  min_leaf_keys_ = static_cast<uint16_t>((static_cast<unsigned>(leaf_cap_) + 1u) / 2u);
  if (min_leaf_keys_ == 0) {
    min_leaf_keys_ = 1;
  }
}

FORCE_INLINE GenericTreeSet::~GenericTreeSet() {
  if (size_ > 0) {
    bool in_scope = DerefScope::is_in_deref_scope();
    if (!in_scope) {
      DerefScope::enter();
    }
    clear(*static_cast<DerefScope *>(nullptr));
    if (!in_scope) {
      DerefScope::exit();
    }
  }
}

FORCE_INLINE const uint8_t *
GenericTreeSet::page_deref(const DerefScope &scope, uint64_t ptr_addr) const {
  if (ptr_addr == kNull) {
    return nullptr;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(ptr_addr);
  if (track_page_loads_) {
    page_loads_.fetch_add(1, std::memory_order_relaxed);
  }
  return reinterpret_cast<const uint8_t *>(ptr->deref(scope));
}

FORCE_INLINE uint8_t *
GenericTreeSet::page_deref_mut(const DerefScope &scope, uint64_t ptr_addr) {
  if (ptr_addr == kNull) {
    return nullptr;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(ptr_addr);
  if (track_page_loads_) {
    page_loads_.fetch_add(1, std::memory_order_relaxed);
  }
  return reinterpret_cast<uint8_t *>(ptr->deref_mut(scope));
}

FORCE_INLINE void GenericTreeSet::init_hdr(uint8_t *base, bool is_leaf,
                                           uint64_t parent_ptr,
                                           uint16_t idx_in_parent) {
  BPlusPageHdr *h = hdr_mut(base);
  h->magic = BPlusPageHdr::kMagic;
  h->num_keys = 0;
  h->index_in_parent = idx_in_parent;
  h->is_leaf = is_leaf ? 1 : 0;
  h->reserved = 0;
  h->parent_ptr = parent_ptr;
  h->next_leaf = kNull;
  h->prev_leaf = kNull;
}

FORCE_INLINE uint64_t
GenericTreeSet::allocate_page_raw(const DerefScope &scope, uint16_t bytes) {
  auto *manager = FarMemManagerFactory::get();
  auto *ptr = new GenericUniquePtr();
  bool ok = manager->allocate_generic_unique_ptr_nb(ptr, ds_id_, bytes);
  if (!ok) {
    *ptr = manager->allocate_generic_unique_ptr(ds_id_, bytes);
  }
  return reinterpret_cast<uint64_t>(ptr);
}

FORCE_INLINE void GenericTreeSet::free_page_raw(uint64_t ptr_addr) {
  if (ptr_addr == kNull) {
    return;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(ptr_addr);
  ptr->free();
  delete ptr;
}

FORCE_INLINE uint16_t
GenericTreeSet::leaf_lower_bound(const uint8_t *base, const uint8_t *key) const {
  const BPlusPageHdr *h = hdr(base);
  uint16_t lo = 0, hi = h->num_keys;
  while (lo < hi) {
    uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    int c = cmp_key(key, leaf_key_ptr(base, mid));
    if (c <= 0) {
      hi = mid;
    } else {
      lo = static_cast<uint16_t>(mid + 1);
    }
  }
  return lo;
}

// First child index i with internal_key[i] > key (in sort order). O(log n) in keys.
FORCE_INLINE uint16_t GenericTreeSet::internal_child_index(const uint8_t *base,
                                                           const uint8_t *key) const {
  const BPlusPageHdr *h = hdr(base);
  uint16_t lo = 0, hi = h->num_keys;
  while (lo < hi) {
    uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    if (cmp_key(internal_key_ptr(base, mid), key) <= 0) {
      lo = static_cast<uint16_t>(mid + 1);
    } else {
      hi = mid;
    }
  }
  return lo;
}

// First pos where cmp(sep_key, internal_key[pos]) <= 0, else num_keys.
FORCE_INLINE uint16_t
GenericTreeSet::internal_insert_pos(const uint8_t *base, const uint8_t *sep_key) const {
  const BPlusPageHdr *h = hdr(base);
  uint16_t lo = 0, hi = h->num_keys;
  while (lo < hi) {
    uint16_t mid = static_cast<uint16_t>((lo + hi) / 2);
    if (cmp_key(sep_key, internal_key_ptr(base, mid)) > 0) {
      lo = static_cast<uint16_t>(mid + 1);
    } else {
      hi = mid;
    }
  }
  return lo;
}

FORCE_INLINE uint64_t
GenericTreeSet::leftmost_leaf(const DerefScope &scope, uint64_t node_addr) const {
  uint64_t cur = node_addr;
  for (;;) {
    const uint8_t *base = page_deref(scope, cur);
    const BPlusPageHdr *h = hdr(base);
    if (h->is_leaf) {
      return cur;
    }
    const uint64_t nxt = *internal_child_ptr(const_cast<uint8_t *>(base), 0);
    prefetch_unique_ptr_handle(nxt);
    cur = nxt;
  }
}

FORCE_INLINE uint64_t
GenericTreeSet::rightmost_leaf(const DerefScope &scope, uint64_t node_addr) const {
  uint64_t cur = node_addr;
  for (;;) {
    const uint8_t *base = page_deref(scope, cur);
    const BPlusPageHdr *h = hdr(base);
    if (h->is_leaf) {
      return cur;
    }
    const uint64_t nxt = *internal_child_ptr(const_cast<uint8_t *>(base), h->num_keys);
    prefetch_unique_ptr_handle(nxt);
    cur = nxt;
  }
}

// --- insert helpers -----------------------------------------------------------

namespace {

FORCE_INLINE void memcpy_keys(uint8_t *dst, const uint8_t *src, size_t n,
                              uint16_t item_size) {
  memcpy(dst, src, n * static_cast<size_t>(item_size));
}

} // namespace

FORCE_INLINE bool GenericTreeSet::leaf_insert_full(
    const DerefScope &scope, uint64_t leaf_addr, const uint8_t *key, bool *split,
    uint64_t *new_right_out, uint8_t *sep_key_out) {
  *split = false;
  uint8_t *base = page_deref_mut(scope, leaf_addr);
  BPlusPageHdr *hp = hdr_mut(base);
  uint16_t pos = leaf_lower_bound(base, key);
  if (pos < hp->num_keys &&
      cmp_key(key, leaf_key_ptr(base, pos)) == 0) {
    return false;
  }

  const uint16_t n = hp->num_keys;
  if (n < leaf_cap_) {
    memmove(leaf_key_ptr(base, static_cast<uint16_t>(pos + 1u)),
            leaf_key_ptr(base, pos),
            static_cast<size_t>(n - pos) * item_size_);
    memcpy(leaf_key_ptr(base, pos), key, item_size_);
    hp->num_keys = static_cast<uint16_t>(n + 1u);
    return true;
  }

  std::vector<uint8_t> tmp(
      static_cast<size_t>(n + 1u) * static_cast<size_t>(item_size_));
  for (uint16_t i = 0; i < n; ++i) {
    memcpy(tmp.data() + static_cast<size_t>(i) * item_size_,
           leaf_key_ptr(base, i), item_size_);
  }
  {
    size_t ins = static_cast<size_t>(pos) * item_size_;
    memmove(tmp.data() + ins + item_size_, tmp.data() + ins,
            (n - pos) * static_cast<size_t>(item_size_));
    memcpy(tmp.data() + ins, key, item_size_);
  }

  const uint16_t total = static_cast<uint16_t>(n + 1u);
  uint16_t mid = static_cast<uint16_t>(total / 2);
  uint64_t right = allocate_page_raw(scope, leaf_page_bytes());
  uint8_t *rb = page_deref_mut(scope, right);
  init_hdr(rb, true, hp->parent_ptr, hp->index_in_parent);
  BPlusPageHdr *rh = hdr_mut(rb);

  hp->num_keys = mid;
  rh->num_keys = static_cast<uint16_t>(total - mid);
  memcpy(leaf_key_ptr(base, 0), tmp.data(),
         static_cast<size_t>(mid) * item_size_);
  memcpy(leaf_key_ptr(rb, 0),
         tmp.data() + static_cast<size_t>(mid) * item_size_,
         static_cast<size_t>(rh->num_keys) * item_size_);

  memcpy(sep_key_out, leaf_key_ptr(rb, 0), item_size_);

  rh->next_leaf = hp->next_leaf;
  rh->prev_leaf = leaf_addr;
  if (hp->next_leaf != kNull) {
    uint8_t *nxb = page_deref_mut(scope, hp->next_leaf);
    hdr_mut(nxb)->prev_leaf = right;
  }
  hp->next_leaf = right;

  *split = true;
  *new_right_out = right;
  return true;
}

FORCE_INLINE bool GenericTreeSet::internal_insert_full(
    const DerefScope &scope, uint64_t node_addr, const uint8_t *sep_key,
    uint64_t new_child, bool *split, uint64_t *new_right_out,
    uint8_t *sep_key_out) {
  *split = false;
  uint8_t *base = page_deref_mut(scope, node_addr);
  BPlusPageHdr *hp = hdr_mut(base);

  const uint16_t pos = internal_insert_pos(base, sep_key);

  if (hp->num_keys < internal_cap_) {
    uint16_t k = hp->num_keys;
    for (uint16_t i = k; i > pos; --i) {
      memcpy(internal_key_ptr(base, i), internal_key_ptr(base, i - 1),
             item_size_);
    }
    for (uint16_t i = static_cast<uint16_t>(k + 1); i > static_cast<uint16_t>(pos + 1);
         --i) {
      *internal_child_ptr(base, i) = *internal_child_ptr(base, i - 1);
    }
    memcpy(internal_key_ptr(base, pos), sep_key, item_size_);
    *internal_child_ptr(base, pos + 1) = new_child;

    hp->num_keys++;
    for (uint16_t j = static_cast<uint16_t>(pos + 1); j <= hp->num_keys; ++j) {
      uint64_t ca = *internal_child_ptr(base, j);
      uint8_t *cb = page_deref_mut(scope, ca);
      hdr_mut(cb)->parent_ptr = node_addr;
      hdr_mut(cb)->index_in_parent = j;
    }
    return true;
  }

  std::vector<uint8_t> keys_tmp(
      static_cast<size_t>(internal_cap_ + 1) * item_size_);
  std::vector<uint64_t> ch_tmp(static_cast<size_t>(internal_cap_) + 2);
  for (uint16_t i = 0; i < hp->num_keys; ++i) {
    memcpy(keys_tmp.data() + static_cast<size_t>(i) * item_size_,
           internal_key_ptr(base, i), item_size_);
  }
  for (uint16_t i = 0; i <= hp->num_keys; ++i) {
    ch_tmp[i] = *internal_child_ptr(base, i);
  }
  {
    uint16_t old_k = hp->num_keys;
    for (uint16_t i = old_k; i > pos; --i) {
      memcpy(keys_tmp.data() + static_cast<size_t>(i) * item_size_,
             keys_tmp.data() + static_cast<size_t>(i - 1) * item_size_,
             item_size_);
    }
    for (uint16_t i = static_cast<uint16_t>(old_k + 1); i > static_cast<uint16_t>(pos + 1);
         --i) {
      ch_tmp[i] = ch_tmp[i - 1];
    }
    memcpy(keys_tmp.data() + static_cast<size_t>(pos) * item_size_, sep_key,
           item_size_);
    ch_tmp[pos + 1] = new_child;
  }

  uint16_t total_keys = static_cast<uint16_t>(hp->num_keys + 1);
  uint16_t mid_idx = total_keys / 2;
  memcpy(sep_key_out,
         keys_tmp.data() + static_cast<size_t>(mid_idx) * item_size_,
         item_size_);

  uint64_t right = allocate_page_raw(scope, internal_page_bytes());
  uint8_t *rb = page_deref_mut(scope, right);
  init_hdr(rb, false, hp->parent_ptr, hp->index_in_parent);
  BPlusPageHdr *rh = hdr_mut(rb);

  hp->num_keys = mid_idx;
  rh->num_keys = static_cast<uint16_t>(total_keys - mid_idx - 1);

  memcpy(internal_key_ptr(base, 0), keys_tmp.data(),
         static_cast<size_t>(mid_idx) * item_size_);
  for (uint16_t i = 0; i <= mid_idx; ++i) {
    *internal_child_ptr(base, i) = ch_tmp[i];
    uint8_t *cb = page_deref_mut(scope, ch_tmp[i]);
    hdr_mut(cb)->parent_ptr = node_addr;
    hdr_mut(cb)->index_in_parent = i;
  }

  memcpy(internal_key_ptr(rb, 0),
         keys_tmp.data() +
             static_cast<size_t>(mid_idx + 1) * item_size_,
         static_cast<size_t>(rh->num_keys) * item_size_);
  for (uint16_t i = 0; i <= rh->num_keys; ++i) {
    *internal_child_ptr(rb, i) = ch_tmp[mid_idx + 1 + i];
    uint8_t *cb = page_deref_mut(scope, ch_tmp[mid_idx + 1 + i]);
    hdr_mut(cb)->parent_ptr = right;
    hdr_mut(cb)->index_in_parent = i;
  }

  *split = true;
  *new_right_out = right;
  return true;
}

FORCE_INLINE bool GenericTreeSet::insert(const DerefScope &scope,
                                         const uint8_t *data) {
  TreeWriteLock wlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    uint64_t leaf = allocate_page_raw(scope, leaf_page_bytes());
    uint8_t *b = page_deref_mut(scope, leaf);
    init_hdr(b, true, kNull, kRootIndex);
    memcpy(leaf_key_ptr(b, 0), data, item_size_);
    hdr_mut(b)->num_keys = 1;
    root_addr_ = leaf;
    size_++;
    return true;
  }

  uint64_t leaf_addr = kNull;
  {
    uint64_t cur = root_addr_;
    for (;;) {
      uint8_t *cb = page_deref_mut(scope, cur);
      BPlusPageHdr *ch = hdr_mut(cb);
      if (ch->is_leaf) {
        leaf_addr = cur;
        break;
      }
      uint16_t ci = internal_child_index(cb, data);
      const uint64_t nxt = *internal_child_ptr(cb, ci);
      prefetch_unique_ptr_handle(nxt);
      cur = nxt;
    }
  }

  bool split = false;
  uint64_t new_right = kNull;
  std::vector<uint8_t> sep(item_size_);
  if (!leaf_insert_full(scope, leaf_addr, data, &split, &new_right,
                        sep.data())) {
    return false;
  }
  size_++;

  while (split) {
    uint8_t *lb = page_deref_mut(scope, leaf_addr);
    BPlusPageHdr *lh = hdr_mut(lb);
    uint64_t parent = lh->parent_ptr;

    if (parent == kNull) {
      uint64_t new_root = allocate_page_raw(scope, internal_page_bytes());
      uint8_t *pb = page_deref_mut(scope, new_root);
      init_hdr(pb, false, kNull, kRootIndex);
      BPlusPageHdr *ph = hdr_mut(pb);
      ph->num_keys = 1;
      memcpy(internal_key_ptr(pb, 0), sep.data(), item_size_);
      *internal_child_ptr(pb, 0) = leaf_addr;
      *internal_child_ptr(pb, 1) = new_right;

      hdr_mut(lb)->parent_ptr = new_root;
      hdr_mut(lb)->index_in_parent = 0;
      uint8_t *rb = page_deref_mut(scope, new_right);
      hdr_mut(rb)->parent_ptr = new_root;
      hdr_mut(rb)->index_in_parent = 1;

      root_addr_ = new_root;
      split = false;
      break;
    }

    bool psplit = false;
    uint64_t pright = kNull;
    std::vector<uint8_t> psep(item_size_);
    internal_insert_full(scope, parent, sep.data(), new_right, &psplit,
                         &pright, psep.data());
    leaf_addr = parent;
    new_right = pright;
    memcpy(sep.data(), psep.data(), item_size_);
    split = psplit;
  }

  return true;
}

FORCE_INLINE bool GenericTreeSet::contains(const DerefScope &scope,
                                           const uint8_t *data) const {
  TreeReadLock rlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return false;
  }
  uint64_t cur = root_addr_;
  for (;;) {
    const uint8_t *base = page_deref(scope, cur);
    const BPlusPageHdr *h = hdr(base);
    if (h->is_leaf) {
      uint16_t pos = leaf_lower_bound(base, data);
      return pos < h->num_keys &&
             cmp_key(data, leaf_key_ptr(base, pos)) == 0;
    }
    const uint16_t ci = internal_child_index(base, data);
    const uint64_t nxt = *internal_child_ptr(const_cast<uint8_t *>(base), ci);
    prefetch_unique_ptr_handle(nxt);
    cur = nxt;
  }
}

FORCE_INLINE GenericTreeSet::GenericIterator
GenericTreeSet::begin(const DerefScope &scope) const {
  TreeReadLock rlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return GenericIterator(kNull, -1, const_cast<GenericTreeSet *>(this));
  }
  const uint64_t lf = leftmost_leaf(scope, root_addr_);
  return GenericIterator(lf, 0, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::GenericIterator
GenericTreeSet::end(const DerefScope &scope) const {
  return GenericIterator(kNull, -1, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::ReverseGenericIterator
GenericTreeSet::rbegin(const DerefScope &scope) const {
  TreeReadLock rlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return ReverseGenericIterator(kNull, -1,
                                    const_cast<GenericTreeSet *>(this));
  }
  const uint64_t lf = rightmost_leaf(scope, root_addr_);
  const uint8_t *b = page_deref(scope, lf);
  const int32_t idx = static_cast<int32_t>(hdr(b)->num_keys) - 1;
  return ReverseGenericIterator(lf, idx, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::ReverseGenericIterator
GenericTreeSet::rend(const DerefScope &scope) const {
  return ReverseGenericIterator(kNull, -1, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::GenericIterator
GenericTreeSet::find(const DerefScope &scope, const uint8_t *data) const {
  TreeReadLock rlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return end(scope);
  }
  uint64_t cur = root_addr_;
  for (;;) {
    const uint8_t *base = page_deref(scope, cur);
    const BPlusPageHdr *h = hdr(base);
    if (h->is_leaf) {
      const uint16_t pos = leaf_lower_bound(base, data);
      if (pos < h->num_keys &&
          cmp_key(data, leaf_key_ptr(base, pos)) == 0) {
        return GenericIterator(cur, static_cast<int32_t>(pos),
                               const_cast<GenericTreeSet *>(this));
      }
      return end(scope);
    }
    const uint16_t ci = internal_child_index(base, data);
    const uint64_t nxt = *internal_child_ptr(const_cast<uint8_t *>(base), ci);
    prefetch_unique_ptr_handle(nxt);
    cur = nxt;
  }
}

FORCE_INLINE const uint8_t *
GenericTreeSet::min(const DerefScope &scope) const {
  TreeReadLock rlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return nullptr;
  }
  const uint64_t lf = leftmost_leaf(scope, root_addr_);
  const uint8_t *b = page_deref(scope, lf);
  if (hdr(b)->num_keys == 0) {
    return nullptr;
  }
  return leaf_key_ptr(b, 0);
}

FORCE_INLINE const uint8_t *
GenericTreeSet::max(const DerefScope &scope) const {
  TreeReadLock rlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return nullptr;
  }
  const uint64_t lf = rightmost_leaf(scope, root_addr_);
  const uint8_t *b = page_deref(scope, lf);
  if (hdr(b)->num_keys == 0) {
    return nullptr;
  }
  return leaf_key_ptr(b, hdr(b)->num_keys - 1);
}

FORCE_INLINE void GenericTreeSet::range_query(
    const DerefScope &scope, const uint8_t *lo, const uint8_t *hi,
    std::vector<uint8_t> *out_bytes) const {
  out_bytes->clear();
  if (cmp_key(lo, hi) > 0) {
    return;
  }
  uint64_t leaf = kNull;
  uint16_t pos = 0;
  {
    TreeReadLock rlk(tree_mu_, concurrent_access_);
    if (root_addr_ == kNull) {
      return;
    }
    uint64_t cur = root_addr_;
    for (;;) {
      const uint8_t *base = page_deref(scope, cur);
      const BPlusPageHdr *h = hdr(base);
      if (h->is_leaf) {
        pos = leaf_lower_bound(base, lo);
        leaf = cur;
        break;
      }
      const uint16_t ci = internal_child_index(base, lo);
      const uint64_t nxt = *internal_child_ptr(const_cast<uint8_t *>(base), ci);
      prefetch_unique_ptr_handle(nxt);
      cur = nxt;
    }
  }

  while (leaf != kNull) {
    const uint8_t *lb = page_deref(scope, leaf);
    const BPlusPageHdr *lh = hdr(lb);
    const uint64_t nxt_leaf = lh->next_leaf;
    if (nxt_leaf != kNull) {
      prefetch_unique_ptr_handle(nxt_leaf);
    }
    for (uint16_t i = pos; i < lh->num_keys; ++i) {
      const uint8_t *kp = leaf_key_ptr(lb, i);
      if (cmp_key(kp, hi) > 0) {
        return;
      }
      if (cmp_key(kp, lo) >= 0 && cmp_key(kp, hi) <= 0) {
        size_t off = out_bytes->size();
        out_bytes->resize(off + item_size_);
        memcpy(out_bytes->data() + off, kp, item_size_);
      }
    }
    leaf = nxt_leaf;
    pos = 0;
  }
}

FORCE_INLINE uint64_t GenericTreeSet::size() const { return size_; }

FORCE_INLINE bool GenericTreeSet::empty() const { return size_ == 0; }

inline void GenericTreeSet::clear_recursive(const DerefScope &scope,
                                            uint64_t node_addr) {
  uint8_t *base = page_deref_mut(scope, node_addr);
  BPlusPageHdr *h = hdr_mut(base);
  if (!h->is_leaf) {
    for (uint16_t i = 0; i <= h->num_keys; ++i) {
      clear_recursive(scope, *internal_child_ptr(base, i));
    }
  }
  free_page_raw(node_addr);
}

FORCE_INLINE void GenericTreeSet::clear(const DerefScope &scope) {
  TreeWriteLock wlk(tree_mu_, concurrent_access_);
  if (root_addr_ != kNull) {
    clear_recursive(scope, root_addr_);
    root_addr_ = kNull;
    size_ = 0;
  }
}

// -----------------------------------------------------------------------------
// Delete (borrow / merge)
// -----------------------------------------------------------------------------

FORCE_INLINE bool GenericTreeSet::remove_key_leaf(const DerefScope &scope,
                                                  uint64_t leaf_addr,
                                                  const uint8_t *key) {
  uint8_t *base = page_deref_mut(scope, leaf_addr);
  BPlusPageHdr *hp = hdr_mut(base);
  uint16_t pos = leaf_lower_bound(base, key);
  if (pos >= hp->num_keys ||
      cmp_key(key, leaf_key_ptr(base, pos)) != 0) {
    return false;
  }

  memmove(leaf_key_ptr(base, pos), leaf_key_ptr(base, pos + 1),
          static_cast<size_t>(hp->num_keys - pos - 1) * item_size_);
  hp->num_keys--;

  if (leaf_addr == root_addr_) {
    if (hp->num_keys == 0) {
      free_page_raw(leaf_addr);
      root_addr_ = kNull;
    }
    return true;
  }

  if (hp->num_keys >= min_leaf_keys_) {
    return true;
  }

  uint64_t parent = hp->parent_ptr;
  uint8_t *pbase = page_deref_mut(scope, parent);
  BPlusPageHdr *pp = hdr_mut(pbase);
  uint16_t idx = hp->index_in_parent;

  auto try_borrow_right = [&]() -> bool {
    if (idx >= pp->num_keys) {
      return false;
    }
    uint64_t rsib = *internal_child_ptr(pbase, idx + 1);
    uint8_t *rb = page_deref_mut(scope, rsib);
    BPlusPageHdr *rh = hdr_mut(rb);
    if (rh->num_keys <= min_leaf_keys_) {
      return false;
    }

    memcpy(leaf_key_ptr(base, hp->num_keys),
           leaf_key_ptr(rb, 0), item_size_);
    hp->num_keys++;
    memmove(leaf_key_ptr(rb, 0), leaf_key_ptr(rb, 1),
            static_cast<size_t>(rh->num_keys - 1) * item_size_);
    rh->num_keys--;
    memcpy(internal_key_ptr(pbase, idx),
           leaf_key_ptr(rb, 0), item_size_);
    return true;
  };

  auto try_borrow_left = [&]() -> bool {
    if (idx == 0) {
      return false;
    }
    uint64_t lsib = *internal_child_ptr(pbase, idx - 1);
    uint8_t *lb = page_deref_mut(scope, lsib);
    BPlusPageHdr *lh = hdr_mut(lb);
    if (lh->num_keys <= min_leaf_keys_) {
      return false;
    }

    memmove(leaf_key_ptr(base, 1), leaf_key_ptr(base, 0),
            static_cast<size_t>(hp->num_keys) * item_size_);
    memcpy(leaf_key_ptr(base, 0),
           leaf_key_ptr(lb, lh->num_keys - 1), item_size_);
    hp->num_keys++;
    lh->num_keys--;
    memcpy(internal_key_ptr(pbase, idx - 1),
           leaf_key_ptr(base, 0), item_size_);
    return true;
  };

  if (try_borrow_right() || try_borrow_left()) {
    return true;
  }

  if (idx < pp->num_keys) {
    uint64_t rsib = *internal_child_ptr(pbase, idx + 1);
    uint8_t *rb = page_deref_mut(scope, rsib);
    BPlusPageHdr *rh = hdr_mut(rb);
    memcpy(leaf_key_ptr(base, hp->num_keys), leaf_key_ptr(rb, 0),
           static_cast<size_t>(rh->num_keys) * item_size_);
    hp->num_keys += rh->num_keys;

    if (rh->next_leaf != kNull) {
      uint8_t *nx = page_deref_mut(scope, rh->next_leaf);
      hdr_mut(nx)->prev_leaf = leaf_addr;
    }
    hp->next_leaf = rh->next_leaf;

    memmove(internal_key_ptr(pbase, idx),
            internal_key_ptr(pbase, idx + 1),
            static_cast<size_t>(pp->num_keys - idx - 1) * item_size_);
    memmove(internal_child_ptr(pbase, idx + 1),
            internal_child_ptr(pbase, idx + 2),
            static_cast<size_t>(pp->num_keys - idx) * sizeof(uint64_t));

    pp->num_keys--;
    free_page_raw(rsib);

    if (pp->num_keys == 0) {
      uint64_t sole = *internal_child_ptr(pbase, 0);
      if (parent == root_addr_) {
        root_addr_ = sole;
        uint8_t *sb = page_deref_mut(scope, sole);
        hdr_mut(sb)->parent_ptr = kNull;
        hdr_mut(sb)->index_in_parent = kRootIndex;
      } else {
        uint64_t gp = pp->parent_ptr;
        uint8_t *gb = page_deref_mut(scope, gp);
        uint16_t pi = pp->index_in_parent;
        *internal_child_ptr(gb, pi) = sole;
        uint8_t *sb = page_deref_mut(scope, sole);
        hdr_mut(sb)->parent_ptr = gp;
        hdr_mut(sb)->index_in_parent = pi;
      }
      free_page_raw(parent);
    }
    return true;
  }

  if (idx > 0) {
    uint64_t lsib = *internal_child_ptr(pbase, idx - 1);
    uint8_t *lb = page_deref_mut(scope, lsib);
    BPlusPageHdr *lh = hdr_mut(lb);
    memcpy(leaf_key_ptr(lb, lh->num_keys), leaf_key_ptr(base, 0),
           static_cast<size_t>(hp->num_keys) * item_size_);
    lh->num_keys += hp->num_keys;

    if (hp->next_leaf != kNull) {
      uint8_t *nx = page_deref_mut(scope, hp->next_leaf);
      hdr_mut(nx)->prev_leaf = lsib;
    }
    lh->next_leaf = hp->next_leaf;

    memmove(internal_key_ptr(pbase, idx - 1),
            internal_key_ptr(pbase, idx),
            static_cast<size_t>(pp->num_keys - idx) * item_size_);
    memmove(internal_child_ptr(pbase, idx),
            internal_child_ptr(pbase, idx + 1),
            static_cast<size_t>(pp->num_keys - idx + 1) * sizeof(uint64_t));

    pp->num_keys--;
    free_page_raw(leaf_addr);

    if (pp->num_keys == 0) {
      uint64_t sole = *internal_child_ptr(pbase, 0);
      if (parent == root_addr_) {
        root_addr_ = sole;
        uint8_t *sb = page_deref_mut(scope, sole);
        hdr_mut(sb)->parent_ptr = kNull;
        hdr_mut(sb)->index_in_parent = kRootIndex;
      } else {
        uint64_t gp = pp->parent_ptr;
        uint8_t *gb = page_deref_mut(scope, gp);
        uint16_t pi = pp->index_in_parent;
        *internal_child_ptr(gb, pi) = sole;
        uint8_t *sb = page_deref_mut(scope, sole);
        hdr_mut(sb)->parent_ptr = gp;
        hdr_mut(sb)->index_in_parent = pi;
      }
      free_page_raw(parent);
    }
    return true;
  }

  return true;
}

FORCE_INLINE bool GenericTreeSet::remove(const DerefScope &scope,
                                         const uint8_t *data) {
  TreeWriteLock wlk(tree_mu_, concurrent_access_);
  if (root_addr_ == kNull) {
    return false;
  }

  uint64_t leaf_addr = kNull;
  {
    uint64_t cur = root_addr_;
    for (;;) {
      uint8_t *cb = page_deref_mut(scope, cur);
      BPlusPageHdr *ch = hdr_mut(cb);
      if (ch->is_leaf) {
        leaf_addr = cur;
        break;
      }
      uint16_t ci = internal_child_index(cb, data);
      const uint64_t nxt = *internal_child_ptr(cb, ci);
      prefetch_unique_ptr_handle(nxt);
      cur = nxt;
    }
  }

  if (!remove_key_leaf(scope, leaf_addr, data)) {
    return false;
  }
  size_--;
  return true;
}

// -----------------------------------------------------------------------------
// TreeSet<T>
// -----------------------------------------------------------------------------

template <typename T>
FORCE_INLINE TreeSet<T>::TreeSet(uint8_t ds_id)
    : GenericTreeSet(sizeof(T), ds_id) {
  compare_fn_ = [](const uint8_t *a, const uint8_t *b) -> int {
    const T &va = *reinterpret_cast<const T *>(a);
    const T &vb = *reinterpret_cast<const T *>(b);
    if (va < vb) {
      return -1;
    }
    if (vb < va) {
      return 1;
    }
    return 0;
  };
}

template <typename T> FORCE_INLINE TreeSet<T>::~TreeSet() {}

template <typename T>
template <bool Reverse>
FORCE_INLINE TreeSet<T>::IteratorImpl<Reverse>::IteratorImpl()
    : GenericTreeSet::GenericIteratorImpl<Reverse>() {}

template <typename T>
template <bool Reverse>
FORCE_INLINE TreeSet<T>::IteratorImpl<Reverse>::IteratorImpl(
    const GenericTreeSet::GenericIteratorImpl<Reverse> &gi)
    : GenericTreeSet::GenericIteratorImpl<Reverse>(gi) {}

template <typename T>
template <bool Reverse>
FORCE_INLINE TreeSet<T>::IteratorImpl<Reverse>::IteratorImpl(const IteratorImpl &o)
    : GenericTreeSet::GenericIteratorImpl<Reverse>(o) {}

template <typename T>
template <bool Reverse>
FORCE_INLINE TreeSet<T>::template IteratorImpl<Reverse> &
TreeSet<T>::IteratorImpl<Reverse>::operator=(const IteratorImpl &o) {
  GenericTreeSet::GenericIteratorImpl<Reverse>::operator=(o);
  return *this;
}

template <typename T>
template <bool Reverse>
FORCE_INLINE const T &
TreeSet<T>::IteratorImpl<Reverse>::deref(const DerefScope &scope) const {
  return *reinterpret_cast<const T *>(
      GenericTreeSet::GenericIteratorImpl<Reverse>::deref(scope));
}

template <typename T>
FORCE_INLINE TreeSet<T>::Iterator
TreeSet<T>::begin(const DerefScope &scope) const {
  return Iterator(GenericTreeSet::begin(scope));
}

template <typename T>
FORCE_INLINE TreeSet<T>::Iterator TreeSet<T>::end(const DerefScope &scope) const {
  return Iterator(GenericTreeSet::end(scope));
}

template <typename T>
FORCE_INLINE TreeSet<T>::ReverseIterator
TreeSet<T>::rbegin(const DerefScope &scope) const {
  return ReverseIterator(GenericTreeSet::rbegin(scope));
}

template <typename T>
FORCE_INLINE TreeSet<T>::ReverseIterator
TreeSet<T>::rend(const DerefScope &scope) const {
  return ReverseIterator(GenericTreeSet::rend(scope));
}

template <typename T>
FORCE_INLINE bool TreeSet<T>::insert(const DerefScope &scope, const T &data) {
  return GenericTreeSet::insert(scope, reinterpret_cast<const uint8_t *>(&data));
}

template <typename T>
FORCE_INLINE bool TreeSet<T>::remove(const DerefScope &scope, const T &data) {
  return GenericTreeSet::remove(scope, reinterpret_cast<const uint8_t *>(&data));
}

template <typename T>
FORCE_INLINE bool TreeSet<T>::contains(const DerefScope &scope,
                                       const T &data) const {
  return GenericTreeSet::contains(scope,
                                  reinterpret_cast<const uint8_t *>(&data));
}

template <typename T>
FORCE_INLINE TreeSet<T>::Iterator
TreeSet<T>::find(const DerefScope &scope, const T &data) const {
  return Iterator(
      GenericTreeSet::find(scope, reinterpret_cast<const uint8_t *>(&data)));
}

template <typename T>
FORCE_INLINE const T &TreeSet<T>::min(const DerefScope &scope) const {
  const uint8_t *p = GenericTreeSet::min(scope);
  static T zero{};
  if (!p) {
    return zero;
  }
  return *reinterpret_cast<const T *>(p);
}

template <typename T>
FORCE_INLINE const T &TreeSet<T>::max(const DerefScope &scope) const {
  const uint8_t *p = GenericTreeSet::max(scope);
  static T zero{};
  if (!p) {
    return zero;
  }
  return *reinterpret_cast<const T *>(p);
}

template <typename T>
FORCE_INLINE void TreeSet<T>::range_query(const DerefScope &scope, const T &lo,
                                          const T &hi,
                                          std::vector<T> *out) const {
  out->clear();
  std::vector<uint8_t> raw;
  GenericTreeSet::range_query(
      scope, reinterpret_cast<const uint8_t *>(&lo),
      reinterpret_cast<const uint8_t *>(&hi), &raw);
  size_t n = raw.size() / sizeof(T);
  out->resize(n);
  memcpy(out->data(), raw.data(), raw.size());
}

} // namespace far_memory
