#pragma once

#include "deref_scope.hpp"
#include "pointer.hpp"

#include <cassert>
#include <cstdint>
#include <functional>

namespace far_memory {

class FarMemManager;

// Red-Black tree node color
enum class RBColor : uint8_t { Red = 0, Black = 1 };

// Generic TreeSet using Red-Black tree stored in far memory
class GenericRbTreeBaseline {
protected:
  // Node structure stored in far memory
  // Layout: |color(1B)|left_ptr(8B)|right_ptr(8B)|parent_ptr(8B)|data[item_size]|
#pragma pack(push, 1)
  struct RbTreeNode {
    RBColor color;
    uint64_t left_addr;   // Address of left child (0 if null)
    uint64_t right_addr;  // Address of right child (0 if null)
    uint64_t parent_addr; // Address of parent (0 if null/root)
    uint8_t data[0];      // Variable-length data follows

    static constexpr uint64_t kNullAddr = 0;

    bool has_left() const { return left_addr != kNullAddr; }
    bool has_right() const { return right_addr != kNullAddr; }
    bool has_parent() const { return parent_addr != kNullAddr; }
    bool is_red() const { return color == RBColor::Red; }
    bool is_black() const { return color == RBColor::Black; }
  };
#pragma pack(pop)

  static constexpr size_t kNodeHeaderSize = sizeof(RBColor) + 3 * sizeof(uint64_t);

  // Iterator for in-order traversal
  template <bool Reverse> class GenericRbIteratorImpl {
  private:
    uint64_t current_addr_;
    GenericRbTreeBaseline *tree_;
    friend class GenericRbTreeBaseline;

  public:
    GenericRbIteratorImpl();
    GenericRbIteratorImpl(uint64_t addr, GenericRbTreeBaseline *tree);
    GenericRbIteratorImpl(const GenericRbIteratorImpl &o);
    GenericRbIteratorImpl &operator=(const GenericRbIteratorImpl &o);

    void inc(const DerefScope &scope);
    void dec(const DerefScope &scope);
    bool operator==(const GenericRbIteratorImpl &o) const;
    bool operator!=(const GenericRbIteratorImpl &o) const;
    const uint8_t *deref(const DerefScope &scope) const;
    uint8_t *deref_mut(const DerefScope &scope);
    bool is_end() const;
  };

  using GenericRbIterator = GenericRbIteratorImpl</* Reverse = */ false>;
  using ReverseGenericRbIterator = GenericRbIteratorImpl</* Reverse = */ true>;

  const uint16_t item_size_;
  uint64_t root_addr_;
  uint64_t size_;
  uint8_t ds_id_;

  // Comparison function for generic data
  std::function<int(const uint8_t *, const uint8_t *)> compare_fn_;

  // Internal helper functions
  RbTreeNode *get_node(const DerefScope &scope, uint64_t addr) const;
  RbTreeNode *get_node_mut(const DerefScope &scope, uint64_t addr);
  uint64_t allocate_node(const DerefScope &scope);
  void free_node(uint64_t addr);

  // Tree traversal helpers
  uint64_t find_minimum(const DerefScope &scope, uint64_t addr) const;
  uint64_t find_maximum(const DerefScope &scope, uint64_t addr) const;
  uint64_t find_successor(const DerefScope &scope, uint64_t addr) const;
  uint64_t find_predecessor(const DerefScope &scope, uint64_t addr) const;

  // Red-Black tree operations
  void rotate_left(const DerefScope &scope, uint64_t x_addr);
  void rotate_right(const DerefScope &scope, uint64_t x_addr);
  void insert_fixup(const DerefScope &scope, uint64_t z_addr);
  void transplant(const DerefScope &scope, uint64_t u_addr, uint64_t v_addr);
  void remove_fixup(const DerefScope &scope, uint64_t x_addr, uint64_t x_parent_addr);

  // Find node with given key
  uint64_t find_node(const DerefScope &scope, const uint8_t *key) const;

  template <typename T> friend class RbTreeSetBaseline;
  friend class FarMemManager;
  friend class FarMemTest;

  GenericRbTreeBaseline(uint16_t item_size, uint8_t ds_id);

public:
  ~GenericRbTreeBaseline();

  GenericRbIterator begin(const DerefScope &scope) const;
  GenericRbIterator end(const DerefScope &scope) const;
  ReverseGenericRbIterator rbegin(const DerefScope &scope) const;
  ReverseGenericRbIterator rend(const DerefScope &scope) const;

  bool insert(const DerefScope &scope, const uint8_t *data);
  bool remove(const DerefScope &scope, const uint8_t *data);
  bool contains(const DerefScope &scope, const uint8_t *data) const;
  GenericRbIterator find(const DerefScope &scope, const uint8_t *data) const;

  const uint8_t *min(const DerefScope &scope) const;
  const uint8_t *max(const DerefScope &scope) const;

  uint64_t size() const;
  bool empty() const;
  void clear(const DerefScope &scope);
};

// Type-safe TreeSet template
template <typename T> class RbTreeSetBaseline : public GenericRbTreeBaseline {
private:
  template <bool Reverse>
  class IteratorImpl : public GenericRbTreeBaseline::GenericRbIteratorImpl<Reverse> {
  private:
    IteratorImpl(const GenericRbTreeBaseline::GenericRbIteratorImpl<Reverse> &generic_iter);
    friend class RbTreeSetBaseline;

  public:
    IteratorImpl();
    IteratorImpl(const IteratorImpl &o);
    IteratorImpl &operator=(const IteratorImpl &o);
    const T &deref(const DerefScope &scope) const;
  };

public:
  using Iterator = IteratorImpl</* Reverse = */ false>;
  using ReverseIterator = IteratorImpl</* Reverse = */ true>;

  RbTreeSetBaseline(uint8_t ds_id);
  ~RbTreeSetBaseline();

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
};

} // namespace far_memory

#include "internal/treeset_rb_baseline.ipp"
