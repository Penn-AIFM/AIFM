extern "C" {
#include <runtime/runtime.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>

using namespace far_memory;

constexpr static uint64_t kCacheSize = (256ULL << 20);
constexpr static uint64_t kFarMemSize = (8ULL << 30);
constexpr static uint32_t kNumGCThreads = 12;
constexpr static uint32_t kNumConnections = 300;
constexpr static uint32_t kNumNodes = 4096;
constexpr static uint32_t kNumEdges = 60000;

void do_work(FarMemManager *manager) {
  std::cout << "Running " << __FILE__ "..." << std::endl;
  DerefScope scope;
  auto graph = manager->allocate_graph<uint32_t>(kNumNodes, 64);
  graph.set_concurrent_access(false);

  std::mt19937 rng(11);
  std::uniform_int_distribution<uint32_t> node_dist(0, kNumNodes - 1);
  for (uint32_t i = 0; i < kNumEdges; ++i) {
    graph.add_edge(scope, node_dist(rng), node_dist(rng));
    if (unlikely(i % 256 == 0)) {
      scope.renew();
    }
  }

  graph.set_prefetch_enabled(false);
  auto reachable_no_prefetch = graph.bfs_count_reachable(scope, 0);
  graph.set_prefetch_enabled(true);
  auto reachable_prefetch = graph.bfs_count_reachable(scope, 0);
  TEST_ASSERT(reachable_no_prefetch == reachable_prefetch);
  TEST_ASSERT(graph.num_edges() > 0);

  std::cout << "reachable_count=" << reachable_prefetch << std::endl;
  std::cout << "Passed" << std::endl;
}

void _main(void *arg) {
  char **argv = static_cast<char **>(arg);
  std::string ip_addr_port(argv[1]);
  auto raddr = helpers::str_to_netaddr(ip_addr_port);
  std::unique_ptr<FarMemManager> manager =
      std::unique_ptr<FarMemManager>(FarMemManagerFactory::build(
          kCacheSize, kNumGCThreads,
          new TCPDevice(raddr, kNumConnections, kFarMemSize)));
  do_work(manager.get());
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 3) {
    std::cerr << "usage: [cfg_file] [ip_addr:port]" << std::endl;
    return -EINVAL;
  }

  char conf_path[strlen(argv[1]) + 1];
  strcpy(conf_path, argv[1]);
  for (int i = 2; i < argc; ++i) {
    argv[i - 1] = argv[i];
  }

  ret = runtime_init(conf_path, _main, argv);
  if (ret) {
    std::cerr << "failed to start runtime" << std::endl;
    return ret;
  }

  return 0;
}
