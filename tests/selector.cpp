#define main solver_main
#include "../track-a/main.cpp"
#undef main
#include <cassert>
#include <random>

int main() {
  std::mt19937 rng(20261102);
  for (uint32_t mask = 0; mask < 256; ++mask)
    for (uint32_t lo : {0u, 1u, 7u})
      for (uint32_t n : {16u, 24u, 31u}) {
        constexpr int32_t guard = 123456789;
        std::vector<int32_t> a(lo + n + 16, guard), b(a.size(), guard), expected;
        for (uint32_t i = 0; i < n; ++i)
          a[lo + i] = mask >> (i % 8) & 1 ? -int32_t(i + 1) : int32_t(i + 1);
        expected.assign(a.begin() + lo, a.begin() + lo + n);
        std::sort(expected.begin(), expected.end());
        const uint32_t cut = split(a.data(), b.data(), lo, lo + n, 0);
        assert(cut <= n);
        assert(std::all_of(b.begin() + lo, b.begin() + lo + cut, [](int32_t v) { return v < 0; }));
        assert(std::all_of(b.begin() + lo + cut, b.begin() + lo + n, [](int32_t v) { return v >= 0; }));
        assert(std::all_of(b.begin(), b.begin() + lo, [&](int32_t v) { return v == guard; }));
        assert(std::all_of(b.begin() + lo + n, b.end(), [&](int32_t v) { return v == guard; }));
        std::sort(b.begin() + lo, b.begin() + lo + n);
        assert(std::equal(expected.begin(), expected.end(), b.begin() + lo));
      }
  std::puts("PASS 2304 full-mask partition and boundary cases");
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
#ifdef TEST_INPLACE
    multiselect(a.data(), 0, n, k, 3, out);
#else
    multiselect(a.data(), b.data(), 0, n, k, 3, out);
#endif
    for (int j = 0; j < 3; ++j) assert(out[j] == expected[k[j]]);
  }
  std::puts("PASS 20000 exact multiquantile cases");
}
