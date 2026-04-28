#pragma once

/*
 * AIFM TreeSet: B+ tree with page-sized far-memory nodes.
 *
 * Why this beats std::set (and the prior pointer-heavy RBTree) in AIFM:
 * - std::set lives entirely in local DRAM; here the working set lives in far
 *   memory and each cache miss costs a device read_object (network/daemon hop).
 * - A classic BST allocates one small object per key; a single lookup touches
 *   O(log N) distinct far objects, multiplying round trips.
 * - This B+ tree packs many sorted keys into one GenericUniquePtr-backed page.
 *   Depth stays low, so each operation performs at most O(tree_height) page
 *   dereferences — typically one read per tree level, often 2–4 total — instead
 *   of one read per key comparison along a deep chain of tiny nodes.
 * - Range scans follow leaf sibling links, touching sequential pages instead of
 *   chasing parent/child pointers for every successor.
 *
 * Remote access minimization:
 * - Batch keys into one blob per page (block-oriented storage).
 * - Iteration and range_query reuse the same loaded leaf while scanning keys.
 * - page_load_count() counts GenericUniquePtr::deref / deref_mut invocations
 *   (upper bound on swap-in / read_object attempts; FarMem cache may satisfy
 *   some locally without I/O).
 * - Descent and leaf walks issue __builtin_prefetch on the next
 *   GenericUniquePtr* handle (software prefetch) unless AIFM_TREESET_NO_PREFETCH
 *   is defined; it warms the handle in CPU cache, not the far object itself.
 */

#include "deref_scope.hpp"
#include "object.hpp"
#include "pointer.hpp"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <shared_mutex>
#include <vector>

namespace far_memory {

class FarMemManager;

#pragma pack(push, 1)
struct BPlusPageHdr {
  static constexpr uint32_t kMagic = 0x4250312B; // "B+1" little-endian-ish tag
  uint32_t magic;
  uint16_t num_keys;
  uint16_t index_in_parent; // 0xFFFF if root
  uint8_t is_leaf;
  uint8_t reserved;
  uint64_t parent_ptr; // GenericUniquePtr* cast to uint64
  uint64_t next_leaf;
  uint64_t prev_leaf;
};
#pragma pack(pop)

class GenericTreeSet {
protected:
  static constexpr uint64_t kNull = 0;
  static constexpr uint16_t kRootIndex = 0xFFFF;

  const uint16_t item_size_;
  uint16_t leaf_cap_;
  uint16_t internal_cap_;
  uint16_t min_leaf_keys_;

  uint64_t root_addr_; // GenericUniquePtr* to root page
  uint64_t size_;
  uint8_t ds_id_;

  std::function<int(const uint8_t *, const uint8_t *)> compare_fn_;

  mutable std::shared_mutex tree_mu_;
  mutable std::atomic<uint64_t> page_loads_{0};
  // Single-threaded workloads: set to false to skip mutex (unsafe if any concurrent access).
  bool concurrent_access_{true};
  // Set true to count page deref in page_load_count(); false avoids an atomic per deref (default).
  bool track_page_loads_{false};

  // --- Page I/O (local cache vs far memory is handled inside GenericUniquePtr;
  // we count deref attempts as the portable proxy for remote reads.) ---
  const uint8_t *page_deref(const DerefScope &scope, uint64_t ptr_addr) const;
  uint8_t *page_deref_mut(const DerefScope &scope, uint64_t ptr_addr);

  uint16_t leaf_page_bytes() const {
    return static_cast<uint16_t>(sizeof(BPlusPageHdr) +
                                 static_cast<size_t>(leaf_cap_) * item_size_);
  }
  uint16_t internal_page_bytes() const {
    return static_cast<uint16_t>(
        sizeof(BPlusPageHdr) +
        static_cast<size_t>(internal_cap_) * item_size_ +
        static_cast<size_t>(internal_cap_ + 1u) * 8u);
  }

  BPlusPageHdr *hdr_mut(uint8_t *base) const {
    return reinterpret_cast<BPlusPageHdr *>(base);
  }
  const BPlusPageHdr *hdr(const uint8_t *base) const {
    return reinterpret_cast<const BPlusPageHdr *>(base);
  }

  uint8_t *leaf_key_ptr(uint8_t *base, uint16_t i) const {
    return base + sizeof(BPlusPageHdr) + static_cast<size_t>(i) * item_size_;
  }
  const uint8_t *leaf_key_ptr(const uint8_t *base, uint16_t i) const {
    return base + sizeof(BPlusPageHdr) + static_cast<size_t>(i) * item_size_;
  }

  uint8_t *internal_key_ptr(uint8_t *base, uint16_t i) const {
    return base + sizeof(BPlusPageHdr) + static_cast<size_t>(i) * item_size_;
  }
  const uint8_t *internal_key_ptr(const uint8_t *base, uint16_t i) const {
    return base + sizeof(BPlusPageHdr) + static_cast<size_t>(i) * item_size_;
  }

  uint64_t *internal_child_ptr(uint8_t *base, uint16_t i) const {
    return reinterpret_cast<uint64_t *>(
        base + sizeof(BPlusPageHdr) +
        static_cast<size_t>(internal_cap_) * item_size_ +
        static_cast<size_t>(i) * 8u);
  }
  const uint64_t *internal_child_ptr(const uint8_t *base, uint16_t i) const {
    return reinterpret_cast<const uint64_t *>(
        base + sizeof(BPlusPageHdr) +
        static_cast<size_t>(internal_cap_) * item_size_ +
        static_cast<size_t>(i) * 8u);
  }

  int cmp_key(const uint8_t *a, const uint8_t *b) const {
    return compare_fn_(a, b);
  }

  uint16_t leaf_lower_bound(const uint8_t *base, const uint8_t *key) const;
  uint16_t internal_insert_pos(const uint8_t *base, const uint8_t *sep_key) const;
  uint16_t internal_child_index(const uint8_t *base,
                                const uint8_t *key) const;

  uint64_t allocate_page_raw(const DerefScope &scope, uint16_t bytes);
  void free_page_raw(uint64_t ptr_addr);
  void init_hdr(uint8_t *base, bool is_leaf, uint64_t parent_ptr,
                uint16_t idx_in_parent);

  uint64_t leftmost_leaf(const DerefScope &scope, uint64_t node_addr) const;
  uint64_t rightmost_leaf(const DerefScope &scope, uint64_t node_addr) const;

  bool leaf_insert_full(const DerefScope &scope, uint64_t leaf_addr,
                        const uint8_t *key, bool *split,
                        uint64_t *new_right_out, uint8_t *sep_key_out);
  bool internal_insert_full(const DerefScope &scope, uint64_t node_addr,
                            const uint8_t *sep_key, uint64_t new_child,
                            bool *split, uint64_t *new_right_out,
                            uint8_t *sep_key_out);

  bool remove_key_leaf(const DerefScope &scope, uint64_t leaf_addr,
                       const uint8_t *key);

  inline void clear_recursive(const DerefScope &scope, uint64_t node_addr);

  template <typename T> friend class TreeSet;
  friend class FarMemManager;
  friend class FarMemTest;

  GenericTreeSet(uint16_t item_size, uint8_t ds_id);

public:
  ~GenericTreeSet();

  uint8_t ds_id() const { return ds_id_; }

  /// Unset for single-producer / single-thread use to remove mutex overhead (data races if unsafe).
  void set_concurrent_access(bool enable) { concurrent_access_ = enable; }

  /// Unset to skip per-deref page_loads_ counting (faster; use when not measuring I/O).
  void set_track_page_loads(bool enable) { track_page_loads_ = enable; }

  uint64_t page_load_count() const { return page_loads_.load(std::memory_order_relaxed); }
  void reset_page_load_counter() {
    page_loads_.store(0, std::memory_order_relaxed);
  }

  template <bool Reverse> class GenericIteratorImpl {
  private:
    uint64_t leaf_addr_;
    int32_t key_idx_; // -1 => end()
    GenericTreeSet *tree_;
    friend class GenericTreeSet;

  public:
    GenericIteratorImpl();
    GenericIteratorImpl(uint64_t leaf, int32_t idx, GenericTreeSet *t);
    GenericIteratorImpl(const GenericIteratorImpl &o);
    GenericIteratorImpl &operator=(const GenericIteratorImpl &o);

    void inc(const DerefScope &scope);
    void dec(const DerefScope &scope);
    bool operator==(const GenericIteratorImpl &o) const;
    bool operator!=(const GenericIteratorImpl &o) const;
    const uint8_t *deref(const DerefScope &scope) const;
    uint8_t *deref_mut(const DerefScope &scope);
    bool is_end() const;
  };

  using GenericIterator = GenericIteratorImpl</* Reverse = */ false>;
  using ReverseGenericIterator = GenericIteratorImpl</* Reverse = */ true>;

  GenericIterator begin(const DerefScope &scope) const;
  GenericIterator end(const DerefScope &scope) const;
  ReverseGenericIterator rbegin(const DerefScope &scope) const;
  ReverseGenericIterator rend(const DerefScope &scope) const;

  bool insert(const DerefScope &scope, const uint8_t *data);
  bool remove(const DerefScope &scope, const uint8_t *data);
  bool contains(const DerefScope &scope, const uint8_t *data) const;
  GenericIterator find(const DerefScope &scope, const uint8_t *data) const;

  const uint8_t *min(const DerefScope &scope) const;
  const uint8_t *max(const DerefScope &scope) const;

  void range_query(const DerefScope &scope, const uint8_t *lo,
                   const uint8_t *hi, std::vector<uint8_t> *out_bytes) const;

  uint64_t size() const;
  bool empty() const;
  void clear(const DerefScope &scope);
};

template <typename T> class TreeSet : public GenericTreeSet {
private:
  template <bool Reverse>
  class IteratorImpl : public GenericTreeSet::GenericIteratorImpl<Reverse> {
  private:
    IteratorImpl(const GenericTreeSet::GenericIteratorImpl<Reverse> &gi);
    friend class TreeSet;

  public:
    IteratorImpl();
    IteratorImpl(const IteratorImpl &o);
    IteratorImpl &operator=(const IteratorImpl &o);
    const T &deref(const DerefScope &scope) const;
  };

public:
  using Iterator = IteratorImpl</* Reverse = */ false>;
  using ReverseIterator = IteratorImpl</* Reverse = */ true>;

  TreeSet(uint8_t ds_id);
  ~TreeSet();

  Iterator begin(const DerefScope &scope) const;
  Iterator end(const DerefScope &scope) const;
  ReverseIterator rbegin(const DerefScope &scope) const;
  ReverseIterator rend(const DerefScope &scope) const;

  bool insert(const DerefScope &scope, const T &data);
  bool remove(const DerefScope &scope, const T &data);
  bool contains(const DerefScope &scope, const T &data) const;
  Iterator find(const DerefScope &scope, const T &data) const;

  const T &min(const DerefScope &scope) const;
  const T &max(const DerefScope &scope) const;

  void range_query(const DerefScope &scope, const T &lo, const T &hi,
                   std::vector<T> *out) const;
};

} // namespace far_memory
