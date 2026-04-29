#pragma once

#include <algorithm>
#include <cassert>
#include <cstring>
#include <limits>
#include <mutex>

namespace far_memory {

namespace {

struct GraphReadLock {
  std::shared_lock<std::shared_mutex> lock_;
  GraphReadLock(std::shared_mutex &m, bool enable) : lock_(m, std::defer_lock) {
    if (enable) {
      lock_.lock();
    }
  }
};

struct GraphWriteLock {
  std::unique_lock<std::shared_mutex> lock_;
  GraphWriteLock(std::shared_mutex &m, bool enable) : lock_(m, std::defer_lock) {
    if (enable) {
      lock_.lock();
    }
  }
};

#if (defined(__GNUC__) || defined(__clang__))
inline void graph_prefetch_unique_ptr_handle(uint64_t ptr_addr) {
  if (ptr_addr == 0) {
    return;
  }
  const void *p =
      reinterpret_cast<const void *>(static_cast<uintptr_t>(ptr_addr));
  __builtin_prefetch(p, 0, 3);
}
#else
inline void graph_prefetch_unique_ptr_handle(uint64_t) {}
#endif

} // namespace

template <typename NodeId>
FORCE_INLINE Graph<NodeId>::Graph(FarMemManager *manager, uint8_t ds_id,
                                  uint32_t max_nodes,
                                  uint16_t chunk_capacity)
    : manager_(manager), ds_id_(ds_id), max_nodes_(max_nodes),
      adjacency_heads_(max_nodes, kNull) {
  size_t max_entries_fit =
      (Object::kMaxObjectDataSize - sizeof(EdgeChunkHdr)) / sizeof(NodeId);
  if (max_entries_fit < kMinChunkCapacity) {
    max_entries_fit = kMinChunkCapacity;
  }
  uint16_t max_cap = static_cast<uint16_t>(std::min(
      max_entries_fit, static_cast<size_t>(std::numeric_limits<uint16_t>::max())));
  if (chunk_capacity == 0) {
    chunk_capacity_ = max_cap;
  } else {
    chunk_capacity_ = std::max<uint16_t>(kMinChunkCapacity,
                                         std::min(chunk_capacity, max_cap));
  }
}

template <typename NodeId> FORCE_INLINE Graph<NodeId>::~Graph() {
  bool in_scope = DerefScope::is_in_deref_scope();
  if (!in_scope) {
    DerefScope::enter();
  }
  clear(*static_cast<DerefScope *>(nullptr));
  if (!in_scope) {
    DerefScope::exit();
  }
}

template <typename NodeId>
FORCE_INLINE uint16_t Graph<NodeId>::edge_chunk_bytes() const {
  return static_cast<uint16_t>(sizeof(EdgeChunkHdr) +
                               static_cast<size_t>(chunk_capacity_) *
                                   sizeof(NodeId));
}

template <typename NodeId>
FORCE_INLINE typename Graph<NodeId>::EdgeChunkHdr *
Graph<NodeId>::hdr_mut(uint8_t *base) const {
  return reinterpret_cast<EdgeChunkHdr *>(base);
}

template <typename NodeId>
FORCE_INLINE const typename Graph<NodeId>::EdgeChunkHdr *
Graph<NodeId>::hdr(const uint8_t *base) const {
  return reinterpret_cast<const EdgeChunkHdr *>(base);
}

template <typename NodeId>
FORCE_INLINE uint8_t *Graph<NodeId>::chunk_key_ptr(uint8_t *base,
                                                   uint16_t i) const {
  return base + sizeof(EdgeChunkHdr) + static_cast<size_t>(i) * sizeof(NodeId);
}

template <typename NodeId>
FORCE_INLINE const uint8_t *Graph<NodeId>::chunk_key_ptr(const uint8_t *base,
                                                         uint16_t i) const {
  return base + sizeof(EdgeChunkHdr) + static_cast<size_t>(i) * sizeof(NodeId);
}

template <typename NodeId>
FORCE_INLINE const uint8_t *
Graph<NodeId>::chunk_deref(const DerefScope &scope, uint64_t ptr_addr) const {
  if (ptr_addr == kNull) {
    return nullptr;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(ptr_addr);
  if (track_chunk_loads_) {
    chunk_loads_.fetch_add(1, std::memory_order_relaxed);
  }
  return reinterpret_cast<const uint8_t *>(ptr->deref(scope));
}

template <typename NodeId>
FORCE_INLINE uint8_t *
Graph<NodeId>::chunk_deref_mut(const DerefScope &scope, uint64_t ptr_addr) {
  if (ptr_addr == kNull) {
    return nullptr;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(ptr_addr);
  if (track_chunk_loads_) {
    chunk_loads_.fetch_add(1, std::memory_order_relaxed);
  }
  return reinterpret_cast<uint8_t *>(ptr->deref_mut(scope));
}

template <typename NodeId>
FORCE_INLINE uint64_t Graph<NodeId>::allocate_chunk(const DerefScope &scope) {
  auto *ptr = new GenericUniquePtr();
  bool ok = manager_->allocate_generic_unique_ptr_nb(ptr, ds_id_,
                                                     edge_chunk_bytes());
  if (!ok) {
    *ptr = manager_->allocate_generic_unique_ptr(ds_id_, edge_chunk_bytes());
  }
  uint8_t *base = reinterpret_cast<uint8_t *>(ptr->deref_mut(scope));
  auto *h = hdr_mut(base);
  h->count = 0;
  h->reserved = 0;
  h->next = kNull;
  return reinterpret_cast<uint64_t>(ptr);
}

template <typename NodeId>
FORCE_INLINE void Graph<NodeId>::free_chunk(uint64_t chunk_addr) {
  if (chunk_addr == kNull) {
    return;
  }
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(chunk_addr);
  ptr->free();
  delete ptr;
}

template <typename NodeId>
FORCE_INLINE void Graph<NodeId>::prefetch_chunk(uint64_t chunk_addr) const {
  if (!prefetch_enabled_ || chunk_addr == kNull) {
    return;
  }
  graph_prefetch_unique_ptr_handle(chunk_addr);
  auto *ptr = reinterpret_cast<GenericUniquePtr *>(chunk_addr);
  ptr->swap_in(false);
}

template <typename NodeId>
FORCE_INLINE bool Graph<NodeId>::contains_edge(const DerefScope &scope,
                                               NodeId src, NodeId dst) const {
  GraphReadLock rlk(mu_, concurrent_access_);
  if (src >= max_nodes_ || dst >= max_nodes_) {
    return false;
  }
  uint64_t cur = adjacency_heads_[src];
  while (cur != kNull) {
    const uint8_t *base = chunk_deref(scope, cur);
    const auto *h = hdr(base);
    const uint64_t next = h->next;
    if (next != kNull) {
      prefetch_chunk(next);
    }
    for (uint16_t i = 0; i < h->count; ++i) {
      NodeId v;
      memcpy(&v, chunk_key_ptr(base, i), sizeof(NodeId));
      if (v == dst) {
        return true;
      }
    }
    cur = next;
  }
  return false;
}

template <typename NodeId>
FORCE_INLINE bool Graph<NodeId>::add_edge(const DerefScope &scope, NodeId src,
                                          NodeId dst) {
  GraphWriteLock wlk(mu_, concurrent_access_);
  if (src >= max_nodes_ || dst >= max_nodes_) {
    return false;
  }
  uint64_t *head = &adjacency_heads_[src];
  if (*head == kNull) {
    *head = allocate_chunk(scope);
  }

  uint64_t cur = *head;
  uint64_t tail = cur;
  while (cur != kNull) {
    uint8_t *base = chunk_deref_mut(scope, cur);
    auto *h = hdr_mut(base);
    const uint64_t next = h->next;
    if (next != kNull) {
      prefetch_chunk(next);
    }
    for (uint16_t i = 0; i < h->count; ++i) {
      NodeId existing;
      memcpy(&existing, chunk_key_ptr(base, i), sizeof(NodeId));
      if (existing == dst) {
        return false;
      }
    }
    tail = cur;
    cur = next;
  }

  uint8_t *tb = chunk_deref_mut(scope, tail);
  auto *th = hdr_mut(tb);
  if (th->count < chunk_capacity_) {
    memcpy(chunk_key_ptr(tb, th->count), &dst, sizeof(NodeId));
    th->count++;
  } else {
    uint64_t new_chunk = allocate_chunk(scope);
    th->next = new_chunk;
    uint8_t *nb = chunk_deref_mut(scope, new_chunk);
    auto *nh = hdr_mut(nb);
    memcpy(chunk_key_ptr(nb, 0), &dst, sizeof(NodeId));
    nh->count = 1;
  }
  num_edges_++;
  return true;
}

template <typename NodeId>
FORCE_INLINE bool Graph<NodeId>::add_undirected_edge(const DerefScope &scope,
                                                     NodeId u, NodeId v) {
  bool added_uv = add_edge(scope, u, v);
  bool added_vu = add_edge(scope, v, u);
  return added_uv && added_vu;
}

template <typename NodeId>
FORCE_INLINE uint64_t Graph<NodeId>::out_degree(const DerefScope &scope,
                                                NodeId src) const {
  GraphReadLock rlk(mu_, concurrent_access_);
  if (src >= max_nodes_) {
    return 0;
  }
  uint64_t degree = 0;
  uint64_t cur = adjacency_heads_[src];
  while (cur != kNull) {
    const uint8_t *base = chunk_deref(scope, cur);
    const auto *h = hdr(base);
    const uint64_t next = h->next;
    if (next != kNull) {
      prefetch_chunk(next);
    }
    degree += h->count;
    cur = next;
  }
  return degree;
}

template <typename NodeId>
FORCE_INLINE void Graph<NodeId>::neighbors(const DerefScope &scope, NodeId src,
                                           std::vector<NodeId> *out) const {
  out->clear();
  GraphReadLock rlk(mu_, concurrent_access_);
  if (src >= max_nodes_) {
    return;
  }
  uint64_t cur = adjacency_heads_[src];
  while (cur != kNull) {
    const uint8_t *base = chunk_deref(scope, cur);
    const auto *h = hdr(base);
    const uint64_t next = h->next;
    if (next != kNull) {
      prefetch_chunk(next);
    }
    for (uint16_t i = 0; i < h->count; ++i) {
      NodeId v;
      memcpy(&v, chunk_key_ptr(base, i), sizeof(NodeId));
      out->push_back(v);
    }
    cur = next;
  }
}

template <typename NodeId>
FORCE_INLINE uint64_t
Graph<NodeId>::bfs_count_reachable(const DerefScope &scope, NodeId src,
                                   uint64_t max_visits) const {
  GraphReadLock rlk(mu_, concurrent_access_);
  if (src >= max_nodes_ || max_visits == 0) {
    return 0;
  }

  std::vector<uint8_t> visited(max_nodes_, 0);
  std::vector<NodeId> queue;
  queue.reserve(std::min<uint64_t>(max_nodes_, max_visits));
  visited[src] = 1;
  queue.push_back(src);
  uint64_t seen = 1;
  size_t head = 0;

  while (head < queue.size() && seen < max_visits) {
    NodeId cur_node = queue[head++];
    uint64_t chunk = adjacency_heads_[cur_node];
    while (chunk != kNull && seen < max_visits) {
      const uint8_t *base = chunk_deref(scope, chunk);
      const auto *h = hdr(base);
      const uint64_t next = h->next;
      if (next != kNull) {
        prefetch_chunk(next);
      }
      for (uint16_t i = 0; i < h->count; ++i) {
        NodeId v;
        memcpy(&v, chunk_key_ptr(base, i), sizeof(NodeId));
        if (v < max_nodes_ && !visited[v]) {
          visited[v] = 1;
          queue.push_back(v);
          seen++;
          if (seen >= max_visits) {
            break;
          }
        }
      }
      chunk = next;
    }
  }
  return seen;
}

template <typename NodeId>
FORCE_INLINE void Graph<NodeId>::clear(const DerefScope &scope) {
  GraphWriteLock wlk(mu_, concurrent_access_);
  for (uint32_t i = 0; i < max_nodes_; ++i) {
    uint64_t cur = adjacency_heads_[i];
    while (cur != kNull) {
      uint8_t *base = chunk_deref_mut(scope, cur);
      const uint64_t next = hdr_mut(base)->next;
      free_chunk(cur);
      cur = next;
    }
    adjacency_heads_[i] = kNull;
  }
  num_edges_ = 0;
}

} // namespace far_memory
