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

## Parallel scatter and output-worker qualification, October 30

- Preserved the prior exact champion `e946711`. Four parse workers now scatter into disjoint per-worker group spans; all group cursors remain exact. Local 3.1M-row, 199 MB synthetic corpus with 132,624 active groups matched a separately implemented Python oracle byte-for-byte.
- On the 4-CPU-quota local machine, randomized 60-run full-size medians: prior sequential scatter 0.1321 s, parallel scatter with three output workers 0.1223 s. This is synthetic data and not scoring hardware.
- Additional 13-run randomized full-size comparison: four parse/three output 0.1134 s, four parse/four output 0.1082 s, four parse/six output 0.1092 s. All output-worker variants produced identical SHA-256; four output workers selected for the local full-size case. Timing noise and scoring-hardware differences remain.
- The parallel-scatter candidate passed ASan/UBSan and TSan on 310k legal-format rows, an independent Python oracle on 0–3.1M rows and additional seeds, and GitHub CI [run 41086942444](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41086942444): official seed-1 tiny/sample/full hashes, seed-2 sample reference, allowlist validator, offline Docker build and restricted Docker tiny hash.
- Docker base images match the frozen allowlist. Official board T/M/S/D results remain unmeasured; only the official integrator may submit.

## Delimiter scanner qualification, October 30

- Compared the existing scalar endpoint delimiter scan with bounded `memchr` and an eight-byte SWAR scanner. The bounded `memchr` variant won consistently; the SWAR variant and a second `memchr` for method scanning were not promoted.
- Local GCC 14.2, 4-CPU-quota machine, 3.1M legal-format synthetic rows, 18 randomized paired runs per distribution: skewed endpoints baseline 0.09923 s vs bounded `memchr` 0.09010 s; uniform endpoints 0.10737 s vs 0.09909 s; duplicate-heavy latencies 0.09927 s vs 0.08718 s (medians). This is NOT official scoring hardware.
- 40 independent Python-oracle differential cases passed across five seeds and 0–100k rows; a 3.1M-row skewed corpus matched the prior exact champion SHA-256. ASan/UBSan and TSan passed on a 3.1M-row local synthetic workload.
- Peak RSS on skewed corpus: 271,800 KiB prior versus 271,788 KiB candidate, effectively unchanged. No added dependency or source-line count; official CI/hash/Docker requalification required before champion promotion.

## Parse-worker scaling, October 30

- Compared two, three, four, five, six and eight parse workers on a 4-CPU-quota local machine. Five beat four on three 3.1M-row synthetic distributions; six and eight were slower locally, so five is the current measured local champion, not an assertion about six-core scoring hardware.
- Randomized 20-run medians, same bounded-delimiter parser: uniform endpoints 4 workers 0.09550 s vs 5 workers 0.09064 s; Zipf-skewed endpoints 4 workers 0.08920 s vs 5 workers 0.08265 s. Both preserved output SHA-256. Extra worker costs about 1.6 MiB of group metadata, negligible against mapped input; final scored K remains unmeasured.
- Independent Python-oracle cases across five seeds and empty/short/100k-row inputs passed with five workers. ASan/UBSan and TSan passed on 3.1M-row synthetic workload. Official CI must confirm full seed-1 hashes and restricted Docker before promotion.

## Exact percentile selection crossover, October 30

- Reordered large-group selection to find p50 first, then p95 in the upper partition, then p99 in the remaining upper partition; this preserves exact nearest-rank values, including duplicates and uint32 extremes.
- Local GCC -O3 standalone randomized comparisons across 257–120k values showed the reordered selection usually 8–20% faster than the previous descending-rank order; no official hardware score is claimed.
- A separate 50,000-case C++ oracle checked the 32-element crossover and ascending-rank selection against full sort, including all-zero, uint32-max, duplicates and randomized groups.
- On a synthetic 3.1M-row, 132,624-group Zipf/hour distribution, four-thread selection-only 12-run averages were 13.805 ms (old ordering, sort through 256), 12.321 ms (ascending ordering, sort through 256), and 10.404 ms (ascending ordering, sort through 32). A separate octave-skewed latency distribution gave 13.466/12.600/10.896 ms respectively. These are selection-only, local, synthetic measurements, not end-to-end scoring-box T or K.
- Threshold 32 retains sort for very small groups; independent microbenchmarks found selection faster by roughly 16–56% at n=48–256, with no new dependencies or additional source lines. End-to-end official performance and hidden-seed behavior remain to be measured.
