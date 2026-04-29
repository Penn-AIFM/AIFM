#pragma once

#include "deref_scope.hpp"
#include "object.hpp"
#include "pointer.hpp"

#include <atomic>
#include <cstdint>
#include <shared_mutex>
#include <type_traits>
#include <vector>

namespace far_memory {

class FarMemManager;

template <typename NodeId = uint32_t> class Graph {
private:
  static_assert(std::is_integral<NodeId>::value,
                "Graph<NodeId> requires an integral NodeId type");

  struct EdgeChunkHdr {
    uint16_t count;
    uint16_t reserved;
    uint64_t next;
  };

  static constexpr uint64_t kNull = 0;
  static constexpr uint16_t kMinChunkCapacity = 4;

  FarMemManager *manager_;
  uint8_t ds_id_;
  uint32_t max_nodes_;
  uint16_t chunk_capacity_;
  uint64_t num_edges_ = 0;
  std::vector<uint64_t> adjacency_heads_;
  mutable std::shared_mutex mu_;
  bool concurrent_access_ = true;
  bool prefetch_enabled_ = true;
  bool track_chunk_loads_ = false;
  mutable std::atomic<uint64_t> chunk_loads_{0};

  const uint8_t *chunk_deref(const DerefScope &scope, uint64_t ptr_addr) const;
  uint8_t *chunk_deref_mut(const DerefScope &scope, uint64_t ptr_addr);
  uint8_t *chunk_key_ptr(uint8_t *base, uint16_t i) const;
  const uint8_t *chunk_key_ptr(const uint8_t *base, uint16_t i) const;
  EdgeChunkHdr *hdr_mut(uint8_t *base) const;
  const EdgeChunkHdr *hdr(const uint8_t *base) const;
  uint16_t edge_chunk_bytes() const;
  uint64_t allocate_chunk(const DerefScope &scope);
  void free_chunk(uint64_t chunk_addr);
  void prefetch_chunk(uint64_t chunk_addr) const;

public:
  Graph(FarMemManager *manager, uint8_t ds_id, uint32_t max_nodes,
        uint16_t chunk_capacity = 0);
  ~Graph();

  uint8_t ds_id() const { return ds_id_; }
  uint32_t max_nodes() const { return max_nodes_; }
  uint64_t num_edges() const { return num_edges_; }
  bool empty() const { return num_edges_ == 0; }

  void set_concurrent_access(bool enable) { concurrent_access_ = enable; }
  void set_prefetch_enabled(bool enable) { prefetch_enabled_ = enable; }
  bool prefetch_enabled() const { return prefetch_enabled_; }
  void set_track_chunk_loads(bool enable) { track_chunk_loads_ = enable; }
  uint64_t chunk_load_count() const {
    return chunk_loads_.load(std::memory_order_relaxed);
  }
  void reset_chunk_load_counter() {
    chunk_loads_.store(0, std::memory_order_relaxed);
  }

  bool add_edge(const DerefScope &scope, NodeId src, NodeId dst);
  bool add_undirected_edge(const DerefScope &scope, NodeId u, NodeId v);
  bool contains_edge(const DerefScope &scope, NodeId src, NodeId dst) const;
  uint64_t out_degree(const DerefScope &scope, NodeId src) const;
  void neighbors(const DerefScope &scope, NodeId src,
                 std::vector<NodeId> *out) const;
  uint64_t bfs_count_reachable(const DerefScope &scope, NodeId src,
                               uint64_t max_visits = UINT64_MAX) const;
  void clear(const DerefScope &scope);
};

} // namespace far_memory

#include "internal/graph.ipp"
