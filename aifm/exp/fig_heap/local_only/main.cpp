#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <vector>

int main(int argc, char **argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s <N>\n", argv[0]);
    return 1;
  }
  uint64_t N = std::strtoull(argv[1], nullptr, 10);

  std::mt19937_64 eng(0xDEADBEEFULL);
  std::uniform_int_distribution<int> distr;

  std::vector<int> v;
  v.reserve(N);

  auto t0 = std::chrono::steady_clock::now();
  for (uint64_t i = 0; i < N; i++) {
    v.push_back(distr(eng));
    std::push_heap(v.begin(), v.end(), std::greater<int>{});
  }
  auto t1 = std::chrono::steady_clock::now();

  bool ok = true;
  int prev = v.front();
  std::pop_heap(v.begin(), v.end(), std::greater<int>{});
  v.pop_back();
  for (uint64_t i = 1; i < N; i++) {
    int cur = v.front();
    if (prev > cur) {
      ok = false;
    }
    prev = cur;
    std::pop_heap(v.begin(), v.end(), std::greater<int>{});
    v.pop_back();
  }
  auto t2 = std::chrono::steady_clock::now();

  auto us = [](auto a, auto b) {
    return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
  };

  if (!ok) {
    std::printf("FAIL\n");
  }
  std::printf("N=%lu build_us=%ld drain_us=%ld total_us=%ld\n",
              static_cast<unsigned long>(N),
              static_cast<long>(us(t0, t1)),
              static_cast<long>(us(t1, t2)),
              static_cast<long>(us(t0, t2)));
  return ok ? 0 : 1;
}
