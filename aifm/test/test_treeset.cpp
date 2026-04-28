extern "C" {
#include <runtime/runtime.h>
#include <runtime/timer.h>
}

#include "deref_scope.hpp"
#include "device.hpp"
#include "helpers.hpp"
#include "manager.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <set>
#include <vector>

using namespace far_memory;
using namespace std;

constexpr uint64_t kCacheSize = (1ULL << 28);
constexpr uint64_t kFarMemSize = (512ULL << 20);

namespace {

void assert_parity(const set<uint64_t> &ref, TreeSet<uint64_t> &ts,
                   const DerefScope &scope) {
  TEST_ASSERT(ts.size() == ref.size());
  for (auto v : ref) {
    TEST_ASSERT(ts.contains(scope, v));
  }
}

} // namespace

class TreeSetCorrectnessTest {
public:
  void do_work(FarMemManager *manager) {
    cout << "Running " << __FILE__ << "..." << endl;

    DerefScope scope;

    // --- Basic operations vs std::set ---
    {
      auto ts = manager->allocate_treeset<uint64_t>();
      ts.set_concurrent_access(false);
      set<uint64_t> ref;
      TEST_ASSERT(ts.empty());
      TEST_ASSERT(ts.insert(scope, 10ull));
      ref.insert(10);
      TEST_ASSERT(ts.contains(scope, 10ull));
      TEST_ASSERT(!ts.insert(scope, 10ull));
      TEST_ASSERT(ts.insert(scope, 5ull));
      ref.insert(5);
      assert_parity(ref, ts, scope);
      TEST_ASSERT(ts.min(scope) == 5ull);
      TEST_ASSERT(ts.max(scope) == 10ull);
      vector<uint64_t> rq;
      ts.range_query(scope, 4ull, 7ull, &rq);
      TEST_ASSERT(rq.size() == 1 && rq[0] == 5ull);
      ts.range_query(scope, 10ull, 10ull, &rq);
      TEST_ASSERT(rq.size() == 1 && rq[0] == 10ull);
      TEST_ASSERT(ts.remove(scope, 5ull));
      ref.erase(5);
      TEST_ASSERT(!ts.contains(scope, 5ull));
      assert_parity(ref, ts, scope);
    }

    // --- Empty tree edge cases ---
    {
      auto ts = manager->allocate_treeset<uint64_t>();
      ts.set_concurrent_access(false);
      TEST_ASSERT(ts.empty());
      TEST_ASSERT(!ts.contains(scope, 1ull));
      vector<uint64_t> rq;
      ts.range_query(scope, 0ull, 100ull, &rq);
      TEST_ASSERT(rq.empty());
      TEST_ASSERT(!ts.remove(scope, 42ull));
    }

    // --- Large range query ---
    {
      auto ts = manager->allocate_treeset<uint64_t>();
      ts.set_concurrent_access(false);
      const int n = 500;
      for (int i = 0; i < n; ++i) {
        TEST_ASSERT(ts.insert(scope, static_cast<uint64_t>(i)));
      }
      vector<uint64_t> rq;
      ts.range_query(scope, 100ull, 399ull, &rq);
      TEST_ASSERT(rq.size() == 300);
      TEST_ASSERT(rq.front() == 100ull && rq.back() == 399ull);
    }

    // --- Randomized stress vs std::set ---
    {
      mt19937_64 rng(12345);
      auto ts = manager->allocate_treeset<uint64_t>();
      ts.set_concurrent_access(false);
      set<uint64_t> ref;
      const int ops = 8000;
      for (int i = 0; i < ops; ++i) {
        uint64_t k = rng();
        int op = static_cast<int>(rng() % 3);
        if (op == 0) {
          bool ir = ref.insert(k).second;
          bool it = ts.insert(scope, k);
          TEST_ASSERT(ir == it);
        } else if (op == 1) {
          TEST_ASSERT(ts.contains(scope, k) == (ref.count(k) > 0));
        } else {
          bool er = ref.erase(k) > 0;
          bool tr = ts.remove(scope, k);
          TEST_ASSERT(er == tr);
        }
        if (i % 500 == 499) {
          assert_parity(ref, ts, scope);
        }
      }
      assert_parity(ref, ts, scope);
    }

    // --- Iterator order ---
    {
      auto ts = manager->allocate_treeset<uint64_t>();
      ts.set_concurrent_access(false);
      vector<uint64_t> keys = {9, 2, 7, 2, 4};
      for (auto k : keys) {
        ts.insert(scope, k);
      }
      vector<uint64_t> got;
      for (auto it = ts.begin(scope); it != ts.end(scope); it.inc(scope)) {
        got.push_back(it.deref(scope));
      }
      sort(keys.begin(), keys.end());
      keys.erase(unique(keys.begin(), keys.end()), keys.end());
      TEST_ASSERT(got == keys);
    }

    cout << "Passed" << endl;
  }
};

void _main(void *arg) {
  unique_ptr<FarMemManager> manager = unique_ptr<FarMemManager>(
      FarMemManagerFactory::build(kCacheSize, std::nullopt,
                                  new FakeDevice(kFarMemSize)));
  TreeSetCorrectnessTest test;
  test.do_work(manager.get());
}

int main(int argc, char *argv[]) {
  int ret;

  if (argc < 2) {
    cerr << "usage: [cfg_file]" << endl;
    return -EINVAL;
  }

  ret = runtime_init(argv[1], _main, NULL);
  if (ret) {
    cerr << "failed to start runtime" << endl;
    return ret;
  }

  return 0;
}
