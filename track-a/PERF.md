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

## Sampled exact-key hash candidate, October 30

- Replaced full-string hashing with a 64-bit mixed hash of length and three 8-byte samples (front, middle, back), retaining mandatory full-byte equality on every hash match. Collisions therefore affect probing time, never grouping correctness. Handles endpoint lengths 1–125 without out-of-bounds reads or architecture-specific intrinsics.
- Local GCC 14.2 dictionary-lookup microbenchmark on 3.1M Zipf-sampled requests and 1,842 generator-shaped endpoint templates: full std::hash 0.0715 s versus sampled 0.0334 s sequential (eight interleaved repetitions), matching ID checksums. This is a standalone component test, not a full-program result.
- Across 100 independently generated 1,842-name REST-template catalogues, ten total duplicate sample signatures occurred, with at most one per catalogue; exact key comparisons always resolve them. A pathological adversarial catalogue could be slower, so official and additional-seed full-program validation is required.
- Added a single CI full-input paired benchmark against the last verified champion to test whether the component gain survives complete execution. No official scoring-box T/M/S/D claim is made.

## Hardware CRC fingerprint qualification, October 30

- Compared exact-checked 64-bit sampled mixing with SSE4.2 CRC32 over the same length/front/middle/back samples. The CRC is only a dictionary fingerprint; full-string equality remains mandatory, including on hash collisions.
- Local GCC 14.2 synthetic 3.1M-lookup test with 1,842 generator-shaped names: sampled mixing 27.59 ms versus CRC32 21.17 ms sequential; five-thread microbench narrowed the advantage, so end-to-end validation was required.
- [CI run 41111964540](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41111964540) passed official seed-1 tiny/sample/full hashes, seed-2 differential, synthetic oracle, Docker allowlist and offline Docker. Seven-run full seed-1 CI medians against pre-selection commit 47e3db0: 0.093236 s versus 0.081989 s (ratio 0.8794). Different CI runners make comparisons across runs unreliable; no official scoring-box score claimed.

## CRC dictionary and local-group packing, October 30

- CRC32 endpoint fingerprints already carry only 32 significant bits; changing slots from 16 to 8 bytes preserves mandatory full-key collision checks and halves each 4,096-slot dictionary's footprint.
- [CI 41120129110](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41120129110) passed all official seed-1 hashes, seed-2 sample differential, synthetic oracle and restricted Docker. Thirteen-run paired seed-1 full medians: prior `2ce5a1f` 0.091071 s, compact-slot `e8377e8` 0.090357 s (ratio 0.9922).
- Packed worker-local group keys into an 11-bit endpoint ID and hour bits; removed constant division/modulo from the 3.1M-record parallel scatter loop. Global group indices remain unchanged.
- [CI 41120372612](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41120372612) passed the same gates. Thirteen-run paired seed-1 full medians: `e8377e8` 0.105081 s, packed-key `8d82b51` 0.103475 s (ratio 0.9847).
- The paired CI results are hardware-specific, and 0.8%/1.5% changes are not conclusively outside noise; no official scoring-box T/M/S/D or hidden-seed result is claimed.

## Dynamic hourly output scheduling, October 30

- Replaced fixed 18-hour worker partitions with an atomic 72-hour work queue. Each hour owns a distinct output buffer and disjoint latency spans; concatenating buffers by hour preserves byte-exact order. Four output workers remain.
- Local GCC 14.2, 4-CPU-quota host, 3.1M-row synthetic datasets, randomized paired full-program trials: Zipf-skewed 50-run medians 0.070169 s (fixed) vs 0.069359 s (dynamic); uniform 40-run medians 0.078380 s vs 0.076012 s. Gains are small and noisy, not official hardware results.
- 310k-row independent Python oracle matched; 3.1M skewed/uniform outputs matched champion SHA-256. ASan/UBSan and TSan passed on 100k-row synthetic data. Peak RSS was effectively unchanged in earlier 3.1M comparisons. Splitting each hour into 2/4/8 tasks and packing count/error into 64 bits were slower locally and rejected.
- Previous full-input [CI 41120735228](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41120735228) passed official hashes and restricted full Docker. Its prior-vs-prior 13-run median varied by 2%, reinforcing the need for paired, repeated measurements. Official T/M/S/D and hidden-seed scoring remain outstanding.

## Five output workers with hourly work stealing, October 30

- Compared dynamic output worker counts 2/3/4/5/6/8 on a 4-CPU-quota host. Five workers beat four on both 3.1M-row synthetic distributions; two/three were slower, and six/eight offered no consistent additional benefit.
- Randomized 60-run paired full-program medians: Zipf-skewed 0.068446 s (four) vs 0.065432 s (five); uniform 0.076108 s vs 0.073007 s. Approximately 4.1–4.4% lower median wall time locally; not official scoring hardware.
- Five-worker output matched independent 310k-row Python oracle and 3.1M-row full-output SHA-256. ASan/UBSan and TSan passed on 310k synthetic data. This one-line worker-count change does not alter output ordering, source footprint materially, or dependency count. CI/official scoring remain separate gates.

## Decimal formatter full-score gate, October 30

- [CI 41136084173](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41136084173) passed official seed-1 tiny/sample/full hashes, seed-2/3 differential, synthetic oracle, offline Docker allowlist and restricted full-input Docker.
- Thirteen paired full seed-1 CI medians: `a1a061f` std::to_chars 0.111769 s, `624fcec` two-digit lookup 0.111210 s (ratio 0.9950); only 0.50% faster, smaller than likely runner variation.
- Lookup adds 21 nonblank noncomment C++ source lines (277 to 298 before clang-format). With the frozen K formula and unchanged memory/dependencies, the measured T gain is outweighed by the S penalty: estimated K(candidate)/K(champion) = 0.9916. Exact scc-formatted S may differ slightly.
- Reverted the formatter to the earlier CI-passing `a1a061f` source; other algorithms remain unchanged. Requalify this restoration via CI before promoting. No official scoring-box K is claimed.

## Exact endpoint interning deduplication and selection alternatives, October 30

- Refactored the two identical exact-checked CRC32 dictionary insertion paths into one always-inline helper; source shrinks from 277 to 265 nonblank noncomment C++ lines before clang-format, with no dependency changes.
- Local GCC 14.2 on a 4-CPU-quota host, 3.1M-row 219MB Zipf-shaped legal-format corpus: 50 randomized paired full-program medians 0.069893 s (prior) vs 0.070333 s (deduplicated), ratio 1.0063; the 0.6% slowdown is within observed runner noise and potentially outweighed by the source-axis improvement. Output SHA-256 matched byte-for-byte.
- Independent Python-oracle differential on 310k rows matched exactly, and ASan/UBSan plus TSan passed on the same corpus. Original source bytes reconstructed locally and verified against Git blob SHA before tests.
- Explored exact shared-prefix MSD radix multiselection: 9,800 randomized oracle cases passed, selection-only Zipf model 32.934 ms to 25.453 ms at 512-element crossover, but end-to-end 50-run median improved only 1.54% while adding 67 source lines; estimated full K declines, so not promoted.
- Eliminating stored hour strings in favor of reconstructed fixed-span hour labels matched full output but gave no reliable full-program improvement and was not promoted.
- The interning deduplication is a candidate pending official seed hashes, CI paired T and Docker; preserve 6c8c910 as the previous passing champion. No scoring-box result is claimed.

## Seven-byte packed tuples and score gate, October 31

- A 24-bit packed group/error field plus full 32-bit latency reduces each staged record from eight to seven bytes; a compile-time size assertion and exact full-width latency preserve the input contract. No added runtime dependency.
- [CI 41167284476](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41167284476) passed official seed-1 hashes, seed-2/3 sample differential, synthetic oracle and restricted offline Docker. Group-first packed layout: 0.082158 s vs 0.081315 s x86-64-v3 champion median; cgroup memory.peak 40,689,664 vs 43,847,680 bytes.
- [CI 41167386234](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41167386234) passed the same gates for latency-first layout.
- [CI 41167517902](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41167517902) passed a direct 13-run paired comparison: group-first 0.111273 s, latency-first 0.111713 s, x86-64-v3 champion 0.110813 s; corresponding Docker cgroup peaks 40,919,040, 40,693,760 and 44,109,824 bytes. Layouts are effectively tied, so the simpler group-first layout was retained.
- Estimated full T/M/S/D score improvement for group-first over the old champion on the final paired CI runner: **+1.25% K**, assuming one extra counted source line and unchanged D. This is not an official scoring-box result; single cgroup measurements and runner noise remain limitations.
- Reducing tuple reserve slack from /70 to /73 changed cgroup peak by only 20 KiB, because untouched virtual capacity is not charged; reverted. A six-byte tuple with sparse overflow sideband was slower than seven bytes in a local Zipf-shaped count/scatter microbenchmark and was not promoted.

## Sparse 5xx counters and exact six-byte overflow alternative, October 31

- Sparse worker error-key sidebands reduce dense per-worker counters from eight to four bytes per group; the 24-bit group field no longer needs an error bit. Full-key endpoint equality, exact counts and 32-bit latencies remain unchanged.
- [CI 41171045748](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41171045748) passed official seed-1 tiny/sample/full hashes, seed-2/3 differential, synthetic oracle and offline Docker. Paired 13-run full medians: packed x86-64-v3 0.105988 s, sparse 0.103382 s; cgroup peaks 40,685,568 and 41,533,440 bytes.
- A separate six-byte record stores 24-bit latencies and uses an encounter-ordered uint32 overflow sideband for every value >= 16,777,215, including the sentinel itself. Local 100k-row independent Python oracle, 3.1M-row SHA comparison, extreme-latency and CRC-alias cases, ASan/UBSan and TSan passed.
- [CI 41171251765](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41171251765) passed the same official hash/Docker gates and added 24-bit boundary differential cases.
- [CI 41171435480](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41171435480) passed a 31-run paired comparison: packed v3 0.089185 s, sparse 0.085996 s, six-byte 0.087570 s. Cgroup peaks were 40,439,808, 40,988,672 and 39,481,344 bytes respectively. Approximate K gains over packed: sparse +1.39%, six-byte +1.07%, using 267/270/273 estimated code lines and unchanged D; not official scoring-box measurements.
- Restored the marginally score-leading, simpler sparse implementation at source blob `3daf5d6a`; six-byte and packed alternatives remain in Git history. In-place cursor-buffer reuse was slower and did not reduce sampled peak anonymous RSS, so was not promoted.

## Endpoint hashing, first-digit parsing and AVX2 delimiter scanning, October 31

- Tested a single CRC32C instruction over three rotated endpoint samples, retaining full-byte dictionary equality on every fingerprint match. A local 60-catalogue collision/probe study found virtually identical average probes (1.4093 old, 1.4087 new); the construction is an optimization, not a unique identifier.
- [CI 41174690609](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41174690609) passed official seed-1 tiny/sample/full hashes, seed-2/3 differential and restricted offline Docker. Thirty-one paired full medians: prior sparse 0.107069 s, one-CRC 0.106639 s; cgroup peaks 41,312,256 and 41,275,392 bytes.
- Starting latency parsing from the mandatory first decimal digit preserves the frozen nonempty grammar and removes one loop iteration. [CI 41175038825](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41175038825) passed the same gates: paired medians sparse 0.110963 s, hash-only 0.111405 s, combined 0.110319 s; corresponding cgroup peaks 41,299,968, 41,291,776 and 41,279,488 bytes.
- Added bounded 32-byte AVX2 space scanning with a safe `memchr` tail fallback; the Docker target already requires x86-64-v3. Local 3.1M-endpoint scan microbenchmark on AMD EPYC measured 33.12 ms for `memchr` versus 24.86 ms for AVX2. Separate ASan/UBSan edge tests covered endpoint lengths 1–125, short suffixes and unaligned starts.
- [CI 41175190484](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41175190484) passed official seed-1 hashes, seed-2/3 differential, synthetic oracle, Docker allowlist and offline Docker. Thirty-one paired full medians: sparse 0.085658 s, pre-AVX 0.085667 s, AVX2 0.083057 s. Docker cgroup peaks: 41,553,920, 41,267,200 and 41,005,056 bytes respectively.
- With unchanged direct dependencies and approximately ten additional counted source lines, the final AVX2 candidate is estimated to improve full T/M/S/D K by about 1.2–1.3% over the sparse champion on this CI runner. Runner variation and source-count approximation remain; no official scoring-box K or hidden-seed result is claimed. The earlier sparse champion remains recoverable at `d3a0515`.

## Streaming hour-epoch architecture, October 31 (Claude frontier session)

- **Design.** The log is nearly time-ordered (seed 1: no line is ever more than one hour late). Workers claim 256 KiB newline-aligned blocks, parse them into per-hour runs of 6-byte records (latency, endpoint id, 5xx bit) and finalize an hour as soon as the completed file prefix has reached hour+2: counting scatter into an L2-resident array, branchless Lomuto multi-select, formatting, ordered writer thread. A late line that breaks the rule is detected when its block registers and the run is repeated without early finalization (`kNever`), so output stays exact; the random-order differential tests exercise this path.
- **Shared exact dictionary.** One lock-free 8-way bucketed table (CRC32 tag, vector tag compare, full 64-byte masked key compare plus tail memcmp, locked inserts, incremental byte-order list) replaces per-worker dictionaries, remapping and reconciliation. No staging array, group counters or global scatter remain.
- **Memory.** No retained record staging; runs, scratch and output buffers are pooled; lookahead is bounded; the writer starts writeback (`sync_file_range`) and drops older output page cache with `posix_fadvise(DONTNEED)`. Page cache of the 10 MB output was ~25% of the old 41 MB cgroup peak.
- **Exit.** Munmap of the 239 MB mapping cost ~6 ms serially at exit; workers now `madvise(DONTNEED)` their slice in parallel when finished (per-block zapping was slower: TLB shootdowns). `-static` saves ~0.15 ms of loader time.
- **Local evidence (4-core Xeon, not the scoring box).** Seed-1 full, 15-run interleaved: old champion 0.103 s, new 0.045 s; single-thread CPU 220 ms -> ~120 ms. Peak cgroup (v1 child) 39.8 MB -> ~6.5 MB.
- **CI [run 41180197488](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41180197488)** (commit `eabf308`, 4 vCPU): official seed-1 tiny/sample/full hashes, seed-2/3 differential, offline Docker all pass; 31-run paired medians champion `3bd5606` 0.100850 s, prior candidate 0.049023 s, candidate 0.045631 s; Docker cgroup `memory.peak` 41,017,344 vs 6,787,072 bytes.
- **Source size.** Formatted code lines 329 -> ~410 (new architecture), measured as `clang-format --style=LLVM | non-blank non-comment`; S penalty ~(321/410)^0.15 is already included in the K estimates.
- **Rejected / measured.** Per-block `MADV_POPULATE_READ` and `MAP_POPULATE` (slower), `pread` into per-thread buffers (equal to mmap + parallel zap, adds buffers), tail slicing of the last two hours (<1 ms predicted by a 12-worker schedule simulation, +4% CPU), `mmap`/`munmap` churn for scratch (10% slower), larger bucket tables, per-block `madvise` (TLB IPIs).
- **Sanitizers.** ASan/UBSan pass on tiny/sample/synthetic inputs (CI step). ThreadSanitizer reports only the deliberate lock-free dictionary publication: a vector load of a bucket's tags racing with the release store that publishes a new entry. On x86-TSO a lane is seen either old or new and the key image and id are written before the tag, so a hit always sees a complete key; adding acquire reloads to silence TSan cost +3% Ir per line and was not adopted.
- **CI [run 41181389405](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41181389405)** (commit `0ec8214`, 4 vCPU): additionally passes reordered/shuffled/reversed official-sample inputs against the Python reference and ASan/UBSan on tiny/sample. Paired 31-run medians: start champion `3bd5606` 0.103975 s, previous candidate 0.051661 s, candidate 0.048400 s; Docker cgroup `memory.peak` 41,037,824 / 6,377,472 / 8,077,312 bytes (single measurements; local fresh-cgroup runs vary 6-10 MB with page-cache writeback timing).
- **Local oracle checks.** Seed-2 full (3.1M rows) matches the Python reference byte for byte; shuffled and reversed 3.1M-row files match the start champion's hash through the exact fallback (0.22 s, 290 MB, adversarial order only); 1-32 threads give identical output; degenerate single-group files (3.1M identical rows, binary latencies) match the reference.
- **Scaling (4 vCPU Xeon).** 1/2/4 CPUs: 0.133/0.074/0.047 s. Parse alone scales ~3.1x on 4 threads; the rest is the last-two-hours finalize tail, the writer thread and startup.
