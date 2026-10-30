# Track A performance log

## Exact C++ baseline and optimization, October 30, 2026

- Frozen spec and organizer reference checked at official HEAD `39a24d1`; the Track A spec did not change since the October 28 snapshot.
- GCC 14.2 local builds; `-O3 -std=c++20 -pthread -static-libstdc++ -static-libgcc`. Runtime dependencies: none.
- 12 synthetic Python-oracle differential cases pass for the initial, flat-table, and parallel implementations: empty, tiny, 1k to 310k rows, four seeds, uint32 extremes, duplicates, 5xx boundaries, hour/month transitions and endpoint order.
- Official worked example matches byte-for-byte locally. Parallel candidate passes ASan/UBSan (0, 1k, 100k, 310k rows) and TSan (1k and 100k rows).
- Local full-size synthetic corpus: 3.1M legal-format rows, 204 MiB, Zipf-skewed endpoints. **Not official input or scoring hardware**.
- Randomized 20-run local medians: one-thread flat/scatter 0.2604 s, parallel output only 0.2249 s, four parse workers plus three output workers **0.1171 s**; all three produced the identical SHA256 on that corpus. Parallel peak RSS ~253,748 KiB.
- A3 two-pass direct scatter was slower (0.4944 s median versus 0.3252 s single-pass in 12 trials), not promoted.
- GitHub CI verifies official seed-1 tiny/sample/full hashes and a separate seed-2 sample against the reference. Its result and Docker/board run IDs must be checked before release promotion; no official scoring result is claimed here.
