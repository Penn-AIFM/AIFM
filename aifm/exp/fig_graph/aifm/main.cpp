extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <vector>

using namespace far_memory;

constexpr static uint64_t kCacheSize = (256ULL << 20);
constexpr static uint64_t kFarMemSize = (8ULL << 30);
constexpr static uint32_t kNumGCThreads = 12;
constexpr static uint32_t kNumConnections = 300;
constexpr static uint16_t kChunkCapacity = 64;

struct InputArgs {
  uint32_t n;
  uint32_t edge_factor;
  uint32_t trials;
  bool use_tcp;
  netaddr raddr;
};

static std::vector<std::vector<uint32_t>>
build_local_graph_and_load_aifm(const DerefScope &scope, Graph<uint32_t> *graph,
                                uint32_t n, uint32_t edge_factor) {
  std::vector<std::vector<uint32_t>> local_adj(n);
  std::mt19937 rng(0x5EED1234u ^ n ^ (edge_factor << 8));
  for (uint32_t u = 0; u < n; ++u) {
    auto &nbrs = local_adj[u];
    nbrs.reserve(edge_factor);
    for (uint32_t j = 0; j < edge_factor; ++j) {
      uint32_t v = (static_cast<uint32_t>(rng()) ^ (u * 2654435761u) ^
                    (j * 40503u)) %
                   n;
      if (v == u) {
        v = (v + 1) % n;
      }
      if (std::find(nbrs.begin(), nbrs.end(), v) != nbrs.end()) {
        continue;
      }
      nbrs.push_back(v);
      graph->add_edge(scope, u, v);
    }
  }
  return local_adj;
}

static uint64_t bfs_local_reachable(const std::vector<std::vector<uint32_t>> &adj,
                                    uint32_t src) {
  if (src >= adj.size()) {
    return 0;
  }
  std::vector<uint8_t> visited(adj.size(), 0);
  std::queue<uint32_t> q;
  visited[src] = 1;
  q.push(src);
  uint64_t seen = 1;
  while (!q.empty()) {
    uint32_t u = q.front();
    q.pop();
    for (auto v : adj[u]) {
      if (!visited[v]) {
        visited[v] = 1;
        seen++;
        q.push(v);
      }
    }
  }
  return seen;
}

template <typename T> static T avg(const std::vector<T> &v) {
  T sum = 0;
  for (auto x : v) {
    sum += x;
  }
  return v.empty() ? 0 : static_cast<T>(sum / v.size());
}

static void run_once(const InputArgs &args) {
  std::unique_ptr<FarMemManager> manager;
  if (args.use_tcp) {
    manager.reset(FarMemManagerFactory::build(
        kCacheSize, kNumGCThreads,
        new TCPDevice(args.raddr, kNumConnections, kFarMemSize)));
  } else {
    manager.reset(FarMemManagerFactory::build(
        kCacheSize, kNumGCThreads, new FakeDevice(kFarMemSize)));
  }

  DerefScope scope;
  auto graph = manager->allocate_graph<uint32_t>(args.n, kChunkCapacity);
  graph.set_concurrent_access(false);
  graph.set_track_chunk_loads(true);
  graph.set_prefetch_enabled(false);

  auto local_adj =
      build_local_graph_and_load_aifm(scope, &graph, args.n, args.edge_factor);
  const uint64_t edges = graph.num_edges();

  std::vector<uint64_t> local_us, off_us, on_us, off_loads, on_loads;
  local_us.reserve(args.trials);
  off_us.reserve(args.trials);
  on_us.reserve(args.trials);
  off_loads.reserve(args.trials);
  on_loads.reserve(args.trials);

  uint64_t reachable_local = 0;
  uint64_t reachable_aifm_off = 0;
  uint64_t reachable_aifm_on = 0;

  for (uint32_t t = 0; t < args.trials; ++t) {
    auto t0 = microtime();
    reachable_local = bfs_local_reachable(local_adj, 0);
    auto t1 = microtime();
    local_us.push_back(t1 - t0);

    scope.renew();
    graph.set_prefetch_enabled(false);
    graph.reset_chunk_load_counter();
    auto t2 = microtime();
    reachable_aifm_off = graph.bfs_count_reachable(scope, 0);
    auto t3 = microtime();
    off_us.push_back(t3 - t2);
    off_loads.push_back(graph.chunk_load_count());

    scope.renew();
    graph.set_prefetch_enabled(true);
    graph.reset_chunk_load_counter();
    auto t4 = microtime();
    reachable_aifm_on = graph.bfs_count_reachable(scope, 0);
    auto t5 = microtime();
    on_us.push_back(t5 - t4);
    on_loads.push_back(graph.chunk_load_count());
  }

  if (reachable_local != reachable_aifm_off || reachable_local != reachable_aifm_on) {
    std::cerr << "ERROR reachable mismatch local=" << reachable_local
              << " off=" << reachable_aifm_off
              << " on=" << reachable_aifm_on << std::endl;
    return;
  }

  double speedup_on_vs_off =
      avg(off_us) ? static_cast<double>(avg(off_us)) / avg(on_us) : 0.0;
  std::cout << "RESULT backend=" << (args.use_tcp ? "tcp" : "fake")
            << " N=" << args.n << " edges=" << edges
            << " trials=" << args.trials
            << " local_bfs_us=" << avg(local_us)
            << " aifm_no_prefetch_us=" << avg(off_us)
            << " aifm_prefetch_us=" << avg(on_us)
            << " reachable=" << reachable_local
            << " loads_no_prefetch=" << avg(off_loads)
            << " loads_prefetch=" << avg(on_loads)
            << " speedup_on_vs_off=" << speedup_on_vs_off << std::endl;
}

void _main(void *arg) {
  auto **argv = static_cast<char **>(arg);
  InputArgs args{};
  args.n = static_cast<uint32_t>(std::strtoul(argv[1], nullptr, 10));
  args.edge_factor = static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10));
  args.trials = static_cast<uint32_t>(std::strtoul(argv[3], nullptr, 10));
  args.use_tcp = (argv[4] != nullptr);
  if (args.use_tcp) {
    args.raddr = helpers::str_to_netaddr(std::string(argv[4]));
  }
  run_once(args);
}

int main(int argc, char *argv[]) {
  if (argc < 5) {
    std::cerr << "usage: [cfg_file] [N] [edge_factor] [trials] [ip:port optional]"
              << std::endl;
    return -EINVAL;
  }

  char conf_path[strlen(argv[1]) + 1];
  strcpy(conf_path, argv[1]);
  for (int i = 2; i < argc; ++i) {
    argv[i - 1] = argv[i];
  }
  argv[argc - 1] = nullptr;

  int ret = runtime_init(conf_path, _main, argv);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }
  return 0;
}
