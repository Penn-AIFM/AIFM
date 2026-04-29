extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <vector>

using namespace far_memory;

constexpr static uint64_t kCacheSize = (256ULL << 20);
constexpr static uint64_t kFarMemSize = (1ULL << 30);
constexpr static uint32_t kNumGCThreads = 8;
constexpr static uint32_t kNumNodes = 2048;
constexpr static uint32_t kNumEdges = 20000;

void do_work(FarMemManager *manager) {
  std::cout << "Running " << __FILE__ "..." << std::endl;
  DerefScope scope;

  auto graph = manager->allocate_graph<uint32_t>(kNumNodes, 64);
  graph.set_concurrent_access(false);
  graph.set_track_chunk_loads(true);
  graph.set_prefetch_enabled(false);

  std::mt19937 rng(7);
  std::uniform_int_distribution<uint32_t> node_dist(0, kNumNodes - 1);
  std::vector<std::pair<uint32_t, uint32_t>> inserted;
  inserted.reserve(kNumEdges);
  for (uint32_t i = 0; i < kNumEdges; ++i) {
    uint32_t u = node_dist(rng);
    uint32_t v = node_dist(rng);
    bool added = graph.add_edge(scope, u, v);
    if (added) {
      inserted.emplace_back(u, v);
    }
  }

  TEST_ASSERT(graph.num_edges() == inserted.size());
  for (uint32_t i = 0; i < std::min<uint32_t>(inserted.size(), 1000); ++i) {
    TEST_ASSERT(graph.contains_edge(scope, inserted[i].first, inserted[i].second));
  }

  auto reachable_without_prefetch = graph.bfs_count_reachable(scope, 0);
  uint64_t loads_without_prefetch = graph.chunk_load_count();

  graph.reset_chunk_load_counter();
  graph.set_prefetch_enabled(true);

  auto reachable_with_prefetch = graph.bfs_count_reachable(scope, 0);
  uint64_t loads_with_prefetch = graph.chunk_load_count();

  TEST_ASSERT(reachable_without_prefetch == reachable_with_prefetch);

  std::vector<uint32_t> neighbors;
  graph.neighbors(scope, 0, &neighbors);
  TEST_ASSERT(neighbors.size() == graph.out_degree(scope, 0));

  std::cout << "reachable_count=" << reachable_with_prefetch
            << ", chunk_loads_no_prefetch=" << loads_without_prefetch
            << ", chunk_loads_prefetch=" << loads_with_prefetch << std::endl;
  std::cout << "Passed" << std::endl;
}

void _main(void *arg) {
  std::unique_ptr<FarMemManager> manager =
      std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
          kCacheSize, kNumGCThreads, new FakeDevice(kFarMemSize)));
  do_work(manager.get());
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    std::cerr << "usage: [cfg_file]" << std::endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
