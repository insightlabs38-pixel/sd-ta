# Track A performance log

## Exact C++ baseline, October 30, 2026

- Design: mmap parse, full-byte endpoint interning, count/scatter, exact local sorting or multiselect.
- Compiler: GCC 14.2, `-O3 -std=c++20 -static-libstdc++ -static-libgcc`.
- Local differential tests: 12 cases, empty through 310,000 records, four seeds, uint32 extremes, duplicates, statuses, boundaries, bytewise endpoint order. All passed against Python oracle.
- Official worked example: byte-exact locally. Official seed-1 and unseen-seed hashes await GitHub CI.
- Initial 310,000-record synthetic run: 0.05 s wall, 36,544 KiB peak RSS, local development container only.

## Local full-size synthetic comparisons

- Generated 3,100,000 legal-format lines (~204 MiB) with a Zipf-skewed endpoint distribution. This is **not** the official dataset or scoring hardware.
- 20 randomized-order, single-core pinned trials: initial unordered-map baseline median 0.3103 s, exact full-key flat hash table median 0.2645 s. Variance and cross-run contention remain significant.
- Sorting/selection threshold 256 is an additional local candidate; performance varied within host noise. All 12 synthetic differential cases passed after combining it with the flat table.
- Two-pass direct scatter challenger measured 0.4944 s median versus 0.3252 s one-pass on 12 local trials; rejected for now.
- No official board run ID exists yet. Luna records actual board IDs after verified integration.
