#define main solver_main
#include "../track-a/main.cpp"
#undef main
#include <cassert>
#include <random>

int main() {
  std::mt19937 rng(20261102);
  for (uint32_t trial = 0; trial < 20000; ++trial) {
    const uint32_t n = trial % 200 ? 1 + rng() % 1024 : 10000;
    std::vector<int32_t> a(n + 16), b(n + 16), expected;
    for (uint32_t i = 0; i < n; ++i)
      a[i] = trial % 7 == 0 ? INT32_MIN : trial % 7 == 1 ? INT32_MAX :
             trial % 7 == 2 ? int32_t(rng() % 5) : int32_t(rng());
    expected.assign(a.begin(), a.begin() + n);
    std::sort(expected.begin(), expected.end());
    if (trial % 7 == 3) std::copy(expected.begin(), expected.end(), a.begin());
    if (trial % 7 == 4) std::reverse(a.begin(), a.begin() + n);
    const uint32_t k[3] = {(50 * n + 99) / 100 - 1,
                           (95 * n + 99) / 100 - 1,
                           (99 * n + 99) / 100 - 1};
    int32_t out[3];
#ifdef TEST_RADIX
    std::stable_sort(a.begin(), a.begin() + n, [](int32_t x, int32_t y) {
      return bucket(uint32_t(x) ^ 0x80000000) < bucket(uint32_t(y) ^ 0x80000000);
    });
    uint32_t end[16]{};
    for (uint32_t i = 0; i < n; ++i) ++end[bucket(uint32_t(a[i]) ^ 0x80000000)];
    for (int i = 1; i < 16; ++i) end[i] += end[i - 1];
    quantiles(a.data(), 0, end, n, out);
#elif defined(TEST_INPLACE)
    multiselect(a.data(), 0, n, k, 3, out);
#else
    multiselect(a.data(), b.data(), 0, n, k, 3, out);
#endif
    for (int j = 0; j < 3; ++j) assert(out[j] == expected[k[j]]);
  }
  std::puts("PASS 20000 exact multiquantile cases");
}
