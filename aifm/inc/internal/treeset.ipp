#pragma once

#include <cstring>

namespace far_memory {

// ============================================================================
// GenericIteratorImpl
// ============================================================================

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse>::GenericIteratorImpl()
    : current_addr_(TreeNode::kNullAddr), tree_(nullptr) {}

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse>::GenericIteratorImpl(
    uint64_t addr, GenericTreeSet *tree)
    : current_addr_(addr), tree_(tree) {}

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse>::GenericIteratorImpl(
    const GenericIteratorImpl &o)
    : current_addr_(o.current_addr_), tree_(o.tree_) {}

template <bool Reverse>
FORCE_INLINE GenericTreeSet::GenericIteratorImpl<Reverse> &
GenericTreeSet::GenericIteratorImpl<Reverse>::operator=(
    const GenericIteratorImpl &o) {
  current_addr_ = o.current_addr_;
  tree_ = o.tree_;
  return *this;
}

template <bool Reverse>
FORCE_INLINE void
GenericTreeSet::GenericIteratorImpl<Reverse>::inc(const DerefScope &scope) {
  if (current_addr_ == TreeNode::kNullAddr) {
    return;
  }
  if constexpr (Reverse) {
    current_addr_ = tree_->find_predecessor(scope, current_addr_);
  } else {
    current_addr_ = tree_->find_successor(scope, current_addr_);
  }
}

template <bool Reverse>
FORCE_INLINE void
GenericTreeSet::GenericIteratorImpl<Reverse>::dec(const DerefScope &scope) {
  if constexpr (Reverse) {
    current_addr_ = tree_->find_successor(scope, current_addr_);
  } else {
    current_addr_ = tree_->find_predecessor(scope, current_addr_);
  }
}

template <bool Reverse>
FORCE_INLINE bool GenericTreeSet::GenericIteratorImpl<Reverse>::operator==(
    const GenericIteratorImpl &o) const {
  return current_addr_ == o.current_addr_;
}

template <bool Reverse>
FORCE_INLINE bool GenericTreeSet::GenericIteratorImpl<Reverse>::operator!=(
    const GenericIteratorImpl &o) const {
  return current_addr_ != o.current_addr_;
}

template <bool Reverse>
FORCE_INLINE const uint8_t *
GenericTreeSet::GenericIteratorImpl<Reverse>::deref(
    const DerefScope &scope) const {
  if (current_addr_ == TreeNode::kNullAddr) {
    return nullptr;
  }
  TreeNode *node = tree_->get_node(scope, current_addr_);
  return node->data;
}

template <bool Reverse>
FORCE_INLINE uint8_t *
GenericTreeSet::GenericIteratorImpl<Reverse>::deref_mut(
    const DerefScope &scope) {
  if (current_addr_ == TreeNode::kNullAddr) {
    return nullptr;
  }
  TreeNode *node = tree_->get_node_mut(scope, current_addr_);
  return node->data;
}

template <bool Reverse>
FORCE_INLINE bool
GenericTreeSet::GenericIteratorImpl<Reverse>::is_end() const {
  return current_addr_ == TreeNode::kNullAddr;
}

// ============================================================================
// GenericTreeSet
// ============================================================================

FORCE_INLINE GenericTreeSet::GenericTreeSet(uint16_t item_size, uint8_t ds_id)
    : item_size_(item_size), root_addr_(TreeNode::kNullAddr), size_(0),
      ds_id_(ds_id) {}

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

FORCE_INLINE GenericTreeSet::TreeNode *
GenericTreeSet::get_node(const DerefScope &scope, uint64_t addr) const {
  if (addr == TreeNode::kNullAddr) {
    return nullptr;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(addr);
  return reinterpret_cast<TreeNode *>(const_cast<void *>(ptr->deref(scope)));
}

FORCE_INLINE GenericTreeSet::TreeNode *
GenericTreeSet::get_node_mut(const DerefScope &scope, uint64_t addr) {
  if (addr == TreeNode::kNullAddr) {
    return nullptr;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(addr);
  return reinterpret_cast<TreeNode *>(ptr->deref_mut(scope));
}

FORCE_INLINE uint64_t
GenericTreeSet::allocate_node(const DerefScope &scope) {
  auto *manager = FarMemManagerFactory::get();
  uint16_t node_size = kNodeHeaderSize + item_size_;

  // Allocate space for the UniquePtr on the heap
  auto *ptr = new GenericUniquePtr();
  bool success = manager->allocate_generic_unique_ptr_nb(ptr, ds_id_, node_size);
  if (!success) {
    // Blocking allocation
    *ptr = manager->allocate_generic_unique_ptr(ds_id_, node_size);
  }

  // Initialize the node
  TreeNode *node = reinterpret_cast<TreeNode *>(ptr->deref_mut(scope));
  node->color = RBColor::Red;
  node->left_addr = TreeNode::kNullAddr;
  node->right_addr = TreeNode::kNullAddr;
  node->parent_addr = TreeNode::kNullAddr;

  return reinterpret_cast<uint64_t>(ptr);
}

FORCE_INLINE void GenericTreeSet::free_node(uint64_t addr) {
  if (addr == TreeNode::kNullAddr) {
    return;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(addr);
  ptr->free();
  delete ptr;
}

FORCE_INLINE uint64_t GenericTreeSet::find_minimum(const DerefScope &scope,
                                                    uint64_t addr) const {
  if (addr == TreeNode::kNullAddr) {
    return TreeNode::kNullAddr;
  }
  TreeNode *node = get_node(scope, addr);
  while (node->has_left()) {
    addr = node->left_addr;
    node = get_node(scope, addr);
  }
  return addr;
}

FORCE_INLINE uint64_t GenericTreeSet::find_maximum(const DerefScope &scope,
                                                    uint64_t addr) const {
  if (addr == TreeNode::kNullAddr) {
    return TreeNode::kNullAddr;
  }
  TreeNode *node = get_node(scope, addr);
  while (node->has_right()) {
    addr = node->right_addr;
    node = get_node(scope, addr);
  }
  return addr;
}

FORCE_INLINE uint64_t GenericTreeSet::find_successor(const DerefScope &scope,
                                                      uint64_t addr) const {
  if (addr == TreeNode::kNullAddr) {
    return TreeNode::kNullAddr;
  }

  TreeNode *node = get_node(scope, addr);

  // If right subtree exists, successor is minimum of right subtree
  if (node->has_right()) {
    return find_minimum(scope, node->right_addr);
  }

  // Otherwise, go up until we find an ancestor that is a left child
  uint64_t parent_addr = node->parent_addr;
  while (parent_addr != TreeNode::kNullAddr) {
    TreeNode *parent = get_node(scope, parent_addr);
    if (parent->left_addr == addr) {
      return parent_addr;
    }
    addr = parent_addr;
    parent_addr = parent->parent_addr;
  }

  return TreeNode::kNullAddr; // No successor (this was the maximum)
}

FORCE_INLINE uint64_t GenericTreeSet::find_predecessor(const DerefScope &scope,
                                                        uint64_t addr) const {
  if (addr == TreeNode::kNullAddr) {
    return TreeNode::kNullAddr;
  }

  TreeNode *node = get_node(scope, addr);

  // If left subtree exists, predecessor is maximum of left subtree
  if (node->has_left()) {
    return find_maximum(scope, node->left_addr);
  }

  // Otherwise, go up until we find an ancestor that is a right child
  uint64_t parent_addr = node->parent_addr;
  while (parent_addr != TreeNode::kNullAddr) {
    TreeNode *parent = get_node(scope, parent_addr);
    if (parent->right_addr == addr) {
      return parent_addr;
    }
    addr = parent_addr;
    parent_addr = parent->parent_addr;
  }

  return TreeNode::kNullAddr; // No predecessor (this was the minimum)
}

FORCE_INLINE void GenericTreeSet::rotate_left(const DerefScope &scope,
                                               uint64_t x_addr) {
  TreeNode *x = get_node_mut(scope, x_addr);
  uint64_t y_addr = x->right_addr;
  TreeNode *y = get_node_mut(scope, y_addr);

  // Turn y's left subtree into x's right subtree
  x->right_addr = y->left_addr;
  if (y->has_left()) {
    TreeNode *y_left = get_node_mut(scope, y->left_addr);
    y_left->parent_addr = x_addr;
  }

  // Link x's parent to y
  y->parent_addr = x->parent_addr;
  if (!x->has_parent()) {
    root_addr_ = y_addr;
  } else {
    TreeNode *x_parent = get_node_mut(scope, x->parent_addr);
    if (x_parent->left_addr == x_addr) {
      x_parent->left_addr = y_addr;
    } else {
      x_parent->right_addr = y_addr;
    }
  }

  // Put x on y's left
  y->left_addr = x_addr;
  x->parent_addr = y_addr;
}

FORCE_INLINE void GenericTreeSet::rotate_right(const DerefScope &scope,
                                                uint64_t x_addr) {
  TreeNode *x = get_node_mut(scope, x_addr);
  uint64_t y_addr = x->left_addr;
  TreeNode *y = get_node_mut(scope, y_addr);

  // Turn y's right subtree into x's left subtree
  x->left_addr = y->right_addr;
  if (y->has_right()) {
    TreeNode *y_right = get_node_mut(scope, y->right_addr);
    y_right->parent_addr = x_addr;
  }

  // Link x's parent to y
  y->parent_addr = x->parent_addr;
  if (!x->has_parent()) {
    root_addr_ = y_addr;
  } else {
    TreeNode *x_parent = get_node_mut(scope, x->parent_addr);
    if (x_parent->right_addr == x_addr) {
      x_parent->right_addr = y_addr;
    } else {
      x_parent->left_addr = y_addr;
    }
  }

  // Put x on y's right
  y->right_addr = x_addr;
  x->parent_addr = y_addr;
}

FORCE_INLINE void GenericTreeSet::insert_fixup(const DerefScope &scope,
                                                uint64_t z_addr) {
  while (true) {
    TreeNode *z = get_node_mut(scope, z_addr);
    if (!z->has_parent()) {
      break;
    }

    TreeNode *z_parent = get_node(scope, z->parent_addr);
    if (z_parent->is_black()) {
      break;
    }

    uint64_t z_parent_addr = z->parent_addr;
    if (!z_parent->has_parent()) {
      break;
    }

    TreeNode *z_grandparent = get_node(scope, z_parent->parent_addr);
    uint64_t z_grandparent_addr = z_parent->parent_addr;

    if (z_parent_addr == z_grandparent->left_addr) {
      // Parent is left child of grandparent
      uint64_t y_addr = z_grandparent->right_addr; // Uncle
      TreeNode *y = (y_addr != TreeNode::kNullAddr) ? get_node_mut(scope, y_addr) : nullptr;

      if (y != nullptr && y->is_red()) {
        // Case 1: Uncle is red
        TreeNode *z_parent_mut = get_node_mut(scope, z_parent_addr);
        z_parent_mut->color = RBColor::Black;
        y->color = RBColor::Black;
        TreeNode *z_grandparent_mut = get_node_mut(scope, z_grandparent_addr);
        z_grandparent_mut->color = RBColor::Red;
        z_addr = z_grandparent_addr;
      } else {
        if (z_addr == z_parent->right_addr) {
          // Case 2: z is right child
          z_addr = z_parent_addr;
          rotate_left(scope, z_addr);
          z = get_node_mut(scope, z_addr);
          z_parent_addr = z->parent_addr;
        }
        // Case 3: z is left child
        TreeNode *z_parent_mut = get_node_mut(scope, z_parent_addr);
        z_parent_mut->color = RBColor::Black;
        TreeNode *z_grandparent_mut = get_node_mut(scope, z_grandparent_addr);
        z_grandparent_mut->color = RBColor::Red;
        rotate_right(scope, z_grandparent_addr);
      }
    } else {
      // Parent is right child of grandparent (symmetric case)
      uint64_t y_addr = z_grandparent->left_addr; // Uncle
      TreeNode *y = (y_addr != TreeNode::kNullAddr) ? get_node_mut(scope, y_addr) : nullptr;

      if (y != nullptr && y->is_red()) {
        // Case 1: Uncle is red
        TreeNode *z_parent_mut = get_node_mut(scope, z_parent_addr);
        z_parent_mut->color = RBColor::Black;
        y->color = RBColor::Black;
        TreeNode *z_grandparent_mut = get_node_mut(scope, z_grandparent_addr);
        z_grandparent_mut->color = RBColor::Red;
        z_addr = z_grandparent_addr;
      } else {
        if (z_addr == z_parent->left_addr) {
          // Case 2: z is left child
          z_addr = z_parent_addr;
          rotate_right(scope, z_addr);
          z = get_node_mut(scope, z_addr);
          z_parent_addr = z->parent_addr;
        }
        // Case 3: z is right child
        TreeNode *z_parent_mut = get_node_mut(scope, z_parent_addr);
        z_parent_mut->color = RBColor::Black;
        TreeNode *z_grandparent_mut = get_node_mut(scope, z_grandparent_addr);
        z_grandparent_mut->color = RBColor::Red;
        rotate_left(scope, z_grandparent_addr);
      }
    }
  }

  // Root must be black
  if (root_addr_ != TreeNode::kNullAddr) {
    TreeNode *root = get_node_mut(scope, root_addr_);
    root->color = RBColor::Black;
  }
}

FORCE_INLINE void GenericTreeSet::transplant(const DerefScope &scope,
                                              uint64_t u_addr, uint64_t v_addr) {
  TreeNode *u = get_node_mut(scope, u_addr);

  if (!u->has_parent()) {
    root_addr_ = v_addr;
  } else {
    TreeNode *u_parent = get_node_mut(scope, u->parent_addr);
    if (u_parent->left_addr == u_addr) {
      u_parent->left_addr = v_addr;
    } else {
      u_parent->right_addr = v_addr;
    }
  }

  if (v_addr != TreeNode::kNullAddr) {
    TreeNode *v = get_node_mut(scope, v_addr);
    v->parent_addr = u->parent_addr;
  }
}

FORCE_INLINE void GenericTreeSet::remove_fixup(const DerefScope &scope,
                                                uint64_t x_addr,
                                                uint64_t x_parent_addr) {
  while (x_addr != root_addr_) {
    TreeNode *x = (x_addr != TreeNode::kNullAddr) ? get_node(scope, x_addr) : nullptr;
    if (x != nullptr && x->is_red()) {
      break;
    }

    TreeNode *x_parent = get_node_mut(scope, x_parent_addr);

    if (x_addr == x_parent->left_addr) {
      uint64_t w_addr = x_parent->right_addr;
      TreeNode *w = get_node_mut(scope, w_addr);

      if (w->is_red()) {
        // Case 1
        w->color = RBColor::Black;
        x_parent->color = RBColor::Red;
        rotate_left(scope, x_parent_addr);
        x_parent = get_node_mut(scope, x_parent_addr);
        w_addr = x_parent->right_addr;
        w = get_node_mut(scope, w_addr);
      }

      TreeNode *w_left = (w->has_left()) ? get_node(scope, w->left_addr) : nullptr;
      TreeNode *w_right = (w->has_right()) ? get_node(scope, w->right_addr) : nullptr;
      bool w_left_black = (w_left == nullptr || w_left->is_black());
      bool w_right_black = (w_right == nullptr || w_right->is_black());

      if (w_left_black && w_right_black) {
        // Case 2
        w->color = RBColor::Red;
        x_addr = x_parent_addr;
        TreeNode *x_new = get_node(scope, x_addr);
        x_parent_addr = x_new->parent_addr;
      } else {
        if (w_right_black) {
          // Case 3
          if (w_left != nullptr) {
            TreeNode *w_left_mut = get_node_mut(scope, w->left_addr);
            w_left_mut->color = RBColor::Black;
          }
          w->color = RBColor::Red;
          rotate_right(scope, w_addr);
          x_parent = get_node_mut(scope, x_parent_addr);
          w_addr = x_parent->right_addr;
          w = get_node_mut(scope, w_addr);
        }
        // Case 4
        w->color = x_parent->color;
        x_parent->color = RBColor::Black;
        if (w->has_right()) {
          TreeNode *w_right_mut = get_node_mut(scope, w->right_addr);
          w_right_mut->color = RBColor::Black;
        }
        rotate_left(scope, x_parent_addr);
        x_addr = root_addr_;
        break;
      }
    } else {
      // Symmetric case
      uint64_t w_addr = x_parent->left_addr;
      TreeNode *w = get_node_mut(scope, w_addr);

      if (w->is_red()) {
        // Case 1
        w->color = RBColor::Black;
        x_parent->color = RBColor::Red;
        rotate_right(scope, x_parent_addr);
        x_parent = get_node_mut(scope, x_parent_addr);
        w_addr = x_parent->left_addr;
        w = get_node_mut(scope, w_addr);
      }

      TreeNode *w_left = (w->has_left()) ? get_node(scope, w->left_addr) : nullptr;
      TreeNode *w_right = (w->has_right()) ? get_node(scope, w->right_addr) : nullptr;
      bool w_left_black = (w_left == nullptr || w_left->is_black());
      bool w_right_black = (w_right == nullptr || w_right->is_black());

      if (w_left_black && w_right_black) {
        // Case 2
        w->color = RBColor::Red;
        x_addr = x_parent_addr;
        TreeNode *x_new = get_node(scope, x_addr);
        x_parent_addr = x_new->parent_addr;
      } else {
        if (w_left_black) {
          // Case 3
          if (w_right != nullptr) {
            TreeNode *w_right_mut = get_node_mut(scope, w->right_addr);
            w_right_mut->color = RBColor::Black;
          }
          w->color = RBColor::Red;
          rotate_left(scope, w_addr);
          x_parent = get_node_mut(scope, x_parent_addr);
          w_addr = x_parent->left_addr;
          w = get_node_mut(scope, w_addr);
        }
        // Case 4
        w->color = x_parent->color;
        x_parent->color = RBColor::Black;
        if (w->has_left()) {
          TreeNode *w_left_mut = get_node_mut(scope, w->left_addr);
          w_left_mut->color = RBColor::Black;
        }
        rotate_right(scope, x_parent_addr);
        x_addr = root_addr_;
        break;
      }
    }
  }

  if (x_addr != TreeNode::kNullAddr) {
    TreeNode *x = get_node_mut(scope, x_addr);
    x->color = RBColor::Black;
  }
}

FORCE_INLINE uint64_t GenericTreeSet::find_node(const DerefScope &scope,
                                                 const uint8_t *key) const {
  uint64_t current = root_addr_;
  while (current != TreeNode::kNullAddr) {
    TreeNode *node = get_node(scope, current);
    int cmp = compare_fn_(key, node->data);
    if (cmp == 0) {
      return current;
    } else if (cmp < 0) {
      current = node->left_addr;
    } else {
      current = node->right_addr;
    }
  }
  return TreeNode::kNullAddr;
}

FORCE_INLINE GenericTreeSet::GenericIterator
GenericTreeSet::begin(const DerefScope &scope) const {
  uint64_t min_addr = find_minimum(scope, root_addr_);
  return GenericIterator(min_addr, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::GenericIterator
GenericTreeSet::end(const DerefScope &scope) const {
  return GenericIterator(TreeNode::kNullAddr, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::ReverseGenericIterator
GenericTreeSet::rbegin(const DerefScope &scope) const {
  uint64_t max_addr = find_maximum(scope, root_addr_);
  return ReverseGenericIterator(max_addr, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE GenericTreeSet::ReverseGenericIterator
GenericTreeSet::rend(const DerefScope &scope) const {
  return ReverseGenericIterator(TreeNode::kNullAddr, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE bool GenericTreeSet::insert(const DerefScope &scope,
                                          const uint8_t *data) {
  // First check if key already exists
  if (find_node(scope, data) != TreeNode::kNullAddr) {
    return false; // Duplicate key
  }

  // Allocate new node
  uint64_t z_addr = allocate_node(scope);
  TreeNode *z = get_node_mut(scope, z_addr);
  memcpy(z->data, data, item_size_);

  // Standard BST insert
  uint64_t y_addr = TreeNode::kNullAddr;
  uint64_t x_addr = root_addr_;

  while (x_addr != TreeNode::kNullAddr) {
    y_addr = x_addr;
    TreeNode *x = get_node(scope, x_addr);
    int cmp = compare_fn_(data, x->data);
    if (cmp < 0) {
      x_addr = x->left_addr;
    } else {
      x_addr = x->right_addr;
    }
  }

  z->parent_addr = y_addr;

  if (y_addr == TreeNode::kNullAddr) {
    root_addr_ = z_addr;
  } else {
    TreeNode *y = get_node_mut(scope, y_addr);
    int cmp = compare_fn_(data, y->data);
    if (cmp < 0) {
      y->left_addr = z_addr;
    } else {
      y->right_addr = z_addr;
    }
  }

  // Fix Red-Black tree properties
  insert_fixup(scope, z_addr);
  size_++;
  return true;
}

FORCE_INLINE bool GenericTreeSet::remove(const DerefScope &scope,
                                          const uint8_t *data) {
  uint64_t z_addr = find_node(scope, data);
  if (z_addr == TreeNode::kNullAddr) {
    return false; // Key not found
  }

  TreeNode *z = get_node_mut(scope, z_addr);
  RBColor y_original_color = z->color;
  uint64_t x_addr;
  uint64_t x_parent_addr;

  if (!z->has_left()) {
    x_addr = z->right_addr;
    x_parent_addr = z->parent_addr;
    transplant(scope, z_addr, z->right_addr);
  } else if (!z->has_right()) {
    x_addr = z->left_addr;
    x_parent_addr = z->parent_addr;
    transplant(scope, z_addr, z->left_addr);
  } else {
    uint64_t y_addr = find_minimum(scope, z->right_addr);
    TreeNode *y = get_node_mut(scope, y_addr);
    y_original_color = y->color;
    x_addr = y->right_addr;

    if (y->parent_addr == z_addr) {
      x_parent_addr = y_addr;
    } else {
      x_parent_addr = y->parent_addr;
      transplant(scope, y_addr, y->right_addr);
      y = get_node_mut(scope, y_addr);
      y->right_addr = z->right_addr;
      if (z->has_right()) {
        TreeNode *z_right = get_node_mut(scope, z->right_addr);
        z_right->parent_addr = y_addr;
      }
    }

    transplant(scope, z_addr, y_addr);
    y = get_node_mut(scope, y_addr);
    y->left_addr = z->left_addr;
    if (z->has_left()) {
      TreeNode *z_left = get_node_mut(scope, z->left_addr);
      z_left->parent_addr = y_addr;
    }
    y->color = z->color;
  }

  free_node(z_addr);
  size_--;

  if (y_original_color == RBColor::Black) {
    remove_fixup(scope, x_addr, x_parent_addr);
  }

  return true;
}

FORCE_INLINE bool GenericTreeSet::contains(const DerefScope &scope,
                                            const uint8_t *data) const {
  return find_node(scope, data) != TreeNode::kNullAddr;
}

FORCE_INLINE GenericTreeSet::GenericIterator
GenericTreeSet::find(const DerefScope &scope, const uint8_t *data) const {
  uint64_t addr = find_node(scope, data);
  return GenericIterator(addr, const_cast<GenericTreeSet *>(this));
}

FORCE_INLINE const uint8_t *GenericTreeSet::min(const DerefScope &scope) const {
  uint64_t min_addr = find_minimum(scope, root_addr_);
  if (min_addr == TreeNode::kNullAddr) {
    return nullptr;
  }
  TreeNode *node = get_node(scope, min_addr);
  return node->data;
}

FORCE_INLINE const uint8_t *GenericTreeSet::max(const DerefScope &scope) const {
  uint64_t max_addr = find_maximum(scope, root_addr_);
  if (max_addr == TreeNode::kNullAddr) {
    return nullptr;
  }
  TreeNode *node = get_node(scope, max_addr);
  return node->data;
}

FORCE_INLINE uint64_t GenericTreeSet::size() const { return size_; }

FORCE_INLINE bool GenericTreeSet::empty() const { return size_ == 0; }

FORCE_INLINE void GenericTreeSet::clear(const DerefScope &scope) {
  // Post-order traversal to free all nodes
  std::function<void(uint64_t)> clear_subtree = [&](uint64_t addr) {
    if (addr == TreeNode::kNullAddr) {
      return;
    }
    TreeNode *node = get_node(scope, addr);
    clear_subtree(node->left_addr);
    clear_subtree(node->right_addr);
    free_node(addr);
  };

  clear_subtree(root_addr_);
  root_addr_ = TreeNode::kNullAddr;
  size_ = 0;
}

// ============================================================================
// TreeSet<T>
// ============================================================================

template <typename T>
FORCE_INLINE TreeSet<T>::TreeSet(uint8_t ds_id)
    : GenericTreeSet(sizeof(T), ds_id) {
  // Set up comparison function for type T
  compare_fn_ = [](const uint8_t *a, const uint8_t *b) -> int {
    const T &val_a = *reinterpret_cast<const T *>(a);
    const T &val_b = *reinterpret_cast<const T *>(b);
    if (val_a < val_b) return -1;
    if (val_b < val_a) return 1;
    return 0;
  };
}

template <typename T>
FORCE_INLINE TreeSet<T>::~TreeSet() {}

template <typename T>
template <bool Reverse>
FORCE_INLINE TreeSet<T>::IteratorImpl<Reverse>::IteratorImpl()
    : GenericTreeSet::GenericIteratorImpl<Reverse>() {}

template <typename T>
template <bool Reverse>
FORCE_INLINE TreeSet<T>::IteratorImpl<Reverse>::IteratorImpl(
    const GenericTreeSet::GenericIteratorImpl<Reverse> &generic_iter)
    : GenericTreeSet::GenericIteratorImpl<Reverse>(generic_iter) {}

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
FORCE_INLINE TreeSet<T>::Iterator
TreeSet<T>::end(const DerefScope &scope) const {
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
  return GenericTreeSet::contains(scope, reinterpret_cast<const uint8_t *>(&data));
}

template <typename T>
FORCE_INLINE TreeSet<T>::Iterator
TreeSet<T>::find(const DerefScope &scope, const T &data) const {
  return Iterator(GenericTreeSet::find(scope, reinterpret_cast<const uint8_t *>(&data)));
}

template <typename T>
FORCE_INLINE const T &TreeSet<T>::min(const DerefScope &scope) const {
  return *reinterpret_cast<const T *>(GenericTreeSet::min(scope));
}

template <typename T>
FORCE_INLINE const T &TreeSet<T>::max(const DerefScope &scope) const {
  return *reinterpret_cast<const T *>(GenericTreeSet::max(scope));
}

} // namespace far_memory