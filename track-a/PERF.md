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

- **Design.** The log is nearly time-ordered (seed 1: no line is ever more than one hour late). Workers claim 256 KiB newline-aligned blocks, parse them into per-hour runs of 6-byte records (latency, endpoint id, 5xx bit) and finalize an hour as soon as the completed file prefix has reached hour+2: counting scatter into an L2-resident array, branchless Lomuto multi-select, formatting, in-order output by whichever thread completes the next hour. A late line that breaks the rule is detected when its block registers and the run is repeated without early finalization (`kNever`), so output stays exact; the random-order differential tests exercise this path.
- **Shared exact dictionary.** One lock-free 8-way bucketed table (CRC32 tag, vector tag compare, full 64-byte masked key compare plus tail memcmp, locked inserts, incremental byte-order list) replaces per-worker dictionaries, remapping and reconciliation. No staging array, group counters or global scatter remain.
- **Memory.** No retained record staging; runs, scratch and output buffers are pooled; lookahead is bounded; the writing thread starts writeback (`sync_file_range`) and drops older output page cache with `posix_fadvise(DONTNEED)`. Page cache of the 10 MB output was ~25% of the old 41 MB cgroup peak.
- **Exit.** Munmap of the 239 MB mapping cost ~6 ms serially at exit; workers now `madvise(DONTNEED)` their slice in parallel when finished (per-block zapping was slower: TLB shootdowns). `-static` saves ~0.15 ms of loader time.
- **Local evidence (4-core Xeon, not the scoring box).** Seed-1 full, 15-run interleaved: old champion 0.103 s, new 0.045 s; single-thread CPU 220 ms -> ~120 ms. Peak cgroup (v1 child) 39.8 MB -> ~6.5 MB.
- **CI [run 41180197488](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41180197488)** (commit `eabf308`, 4 vCPU): official seed-1 tiny/sample/full hashes, seed-2/3 differential, offline Docker all pass; 31-run paired medians champion `3bd5606` 0.100850 s, prior candidate 0.049023 s, candidate 0.045631 s; Docker cgroup `memory.peak` 41,017,344 vs 6,787,072 bytes.
- **Source size.** Formatted code lines 329 -> ~410 (new architecture), measured as `clang-format --style=LLVM | non-blank non-comment`; S penalty ~(321/410)^0.15 is already included in the K estimates.
- **Rejected / measured.** Per-block `MADV_POPULATE_READ` and `MAP_POPULATE` (slower), `pread` into per-thread buffers (equal to mmap + parallel zap, adds buffers), tail slicing of the last two hours (<1 ms predicted by a 12-worker schedule simulation, +4% CPU), `mmap`/`munmap` churn for scratch (10% slower), larger bucket tables, per-block `madvise` (TLB IPIs).
- **Sanitizers.** ASan/UBSan pass on tiny/sample/synthetic inputs (CI step). ThreadSanitizer reports only the deliberate lock-free dictionary publication: a vector load of a bucket's tags racing with the release store that publishes a new entry. On x86-TSO a lane is seen either old or new and the key image and id are written before the tag, so a hit always sees a complete key; adding acquire reloads to silence TSan cost +3% Ir per line and was not adopted.
- **CI [run 41181389405](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41181389405)** (commit `0ec8214`, 4 vCPU): additionally passes reordered/shuffled/reversed official-sample inputs against the Python reference and ASan/UBSan on tiny/sample. Paired 31-run medians: start champion `3bd5606` 0.103975 s, previous candidate 0.051661 s, candidate 0.048400 s; Docker cgroup `memory.peak` 41,037,824 / 6,377,472 / 8,077,312 bytes (single measurements; local fresh-cgroup runs vary 6-10 MB with page-cache writeback timing).
- **Local oracle checks.** Seed-2 full (3.1M rows) matches the Python reference byte for byte; shuffled and reversed 3.1M-row files match the start champion's hash through the exact fallback (0.22 s, 290 MB, adversarial order only); 1-32 threads give identical output; degenerate single-group files (3.1M identical rows, binary latencies) match the reference.
- **Scaling (4 vCPU Xeon).** 1/2/4 CPUs: 0.133/0.074/0.047 s. Parse alone scales ~3.1x on 4 threads; the rest is the last-two-hours finalize tail, the writer thread and startup.
- **Output without a writer thread.** A dedicated writer thread lagged behind busy workers (11-23 output buffers pending, 1.2 MB more peak). Now the thread that completes an hour writes every consecutive ready hour (others just leave their part): cgroup peak median 7.3 -> 6.0 MB and a tighter spread at 4 threads, wall time unchanged, one thread fewer.
- **Dictionary lock.** All threads discover the hot endpoints in the first millisecond; a sleeping mutex made each handoff cost a futex wake. Inserts hold the lock for ~0.2 us, so a spin-then-yield lock replaced it (neutral at 4 threads, 20% faster than a mutex in a 10x slower-insert emulation, a hedge for 12 real threads).
- **Selection micro-benchmarks** (hot-cache, hour 20, 67k values): 5.6 ns/element; 3.4 ns for groups of 64-256, 2.7 ns above 1024. Sorting ranges of 8 or fewer beat 16 by 5% end-to-end. Floyd-Rivest style bracketing and an AVX2 compress-store partition were estimated at <=3% of total CPU for 35+ source lines and not built.

## Race-free streaming promotion and exact-cache score gate, October 31

- Promoted the previously CI-verified streaming hour-epoch architecture from the development branch. Kept exact out-of-order fallback and immutable full-key endpoint equality. The concurrent dictionary now reads all published tags atomically with acquire semantics; spinlock initialization is explicit. Packed-run allocation has trailing safety slack.
- [CI 41182845042](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41182845042) passed official seed-1 tiny/sample/full hashes, seed-2/3 differential, shuffled/reversed/sorted inputs, ASan/UBSan and offline Docker. 31-run paired full medians: previous x86-64-v3 champion 0.105166 s, race-free streaming 0.057542 s; cgroup memory.peak 41,304,064 vs 5,595,136 bytes. No official scoring-box result claimed.
- Replaced overlapping 8-byte writes into six-byte records with exact six-byte copies; added 40-byte minimum-line density tests around 256-KiB block boundaries and ThreadSanitizer on concurrent tiny/sample runs. [CI 41183043443](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41183043443) passed every gate, including TSan and 200,000 dense records. Paired medians versus the atomic baseline: 0.056181 vs 0.056786 s; cgroup peaks 6,877,184 vs 5,820,416 bytes, yielding approximately +2.9% estimated K with unchanged dependencies and similar source size.
- Tested a 256-entry exact per-block endpoint cache; [CI 41183210789](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41183210789) passed correctness, TSan and Docker, but 31-run paired medians regressed 0.041202 to 0.042476 s and cgroup peaks increased 5,652,480 to 6,262,784 bytes. This loses roughly 3.8% estimated K after source penalty, so the cache was rejected.
- Restored the exact previously passing race-free source from `f7dc17e`; score comparisons are CI-runner estimates, not official six-core T/M/S/D or hidden-seed validation.

## Output allocation, packed endpoint slots and lookahead, October 31

- Per-hour output allocation replaced the pooled fixed buffers. [CI 41187070055](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41187070055) passed exactness, sanitizer and Docker gates, but a direct three-way comparison in [CI 41187281653](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41187281653) favored the pooled baseline. Thirty-one-run full medians: pooled 0.051512 s, allocation-only 0.052357 s, allocation plus packed dictionary 0.051596 s. Five-run maximum cgroup peaks: 6,041,600 / 6,750,208 / 8,212,480 bytes. The allocation-only design was rejected.
- Packed dictionary experiment stored a 21-bit CRC fingerprint and 11-bit endpoint ID in one atomic 32-bit slot; all fingerprint matches still required exact full-key equality. Reduced bucket footprint from 32 to 16 KiB, passed ThreadSanitizer, official hashes, reordered inputs and offline Docker in [CI 41187473228](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41187473228). Paired 31-run medians: pooled champion 0.037703 s, packed-only 0.038021 s, packed plus 16-block lookahead 0.038121 s. Five-run maximum cgroup peaks: 6,660,096 / 7,294,976 / 8,241,152 bytes. Memory peaks vary materially by run, and no robust full-K gain was demonstrated.
- Restored the previously qualified streaming source blob `a4a487a0e1ddebe51f27d1af7eb90c98faf12e0c` from `d9680bd`. All experimental commits remain in history. These measurements are GitHub CI comparisons, not official scoring-box results.

## Latency scratch and tiny-group selection gates, October 31

- Reduced per-hour scratch reserve from 131,072 to 65,536 uint32 slots. [CI 41191489147](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41191489147) passed official seed-1 hashes, seed-2/3, adversarial ordering, sanitizers and offline Docker. Paired 31-run full medians: streaming champion 0.051150 s, candidate 0.051361 s; five-run maximum cgroup peaks 6,774,784 and 6,660,096 bytes. Estimated K gain ~0.14% is below measurement confidence, so not promoted.
- Skipped redundant p95/p99 selection when groups have <=8 records (the first selection already fully sorts them). [CI 41191664263](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41191664263) passed all correctness, sanitizer and Docker gates. Paired medians 0.050776 s champion, 0.050711 s candidate, a 0.13% gain; two additional code lines and unstable cgroup maxima 11,169,792 vs 11,296,768 bytes do not establish a full-K improvement. Not promoted.
- Both variants are recoverable in Git. Restored the exact `d9680bd` streaming source blob `a4a487a0e1ddebe51f27d1af7eb90c98faf12e0c`; no official six-core score or hidden-seed claim.

## LTO and percentile-max experiments, October 31

- Link-time optimization (`-flto`) passed official seed-1 hashes, seed-2/3, reordered input, ASan/UBSan, ThreadSanitizer and restricted offline Docker in [CI 41195173112](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41195173112). Paired 31-run full medians were 0.051793 s for the streaming champion and 0.051614 s for LTO (0.35% faster); five-run maximum cgroup peaks were 5,820,416 and 5,844,992 bytes. Estimated relative K +0.09% with unchanged S/D is below noise; LTO flags were reverted.
- Exact percentile-max shortcuts for n<20 and n<100, with redundant selects omitted for n<=8, passed 500,000 randomized/adversarial cases under local ASan/UBSan. [CI 41195392271](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41195392271) passed official hashes, seed-2/3, reordered inputs, sanitizers and offline Docker. Paired 31-run full medians: champion 0.051204 s, candidate 0.051070 s (0.26% faster); five-run cgroup maxima 5,984,256 and 6,987,776 bytes, with notable peak variability. Eight extra source lines outweigh the tiny speed gain even before the memory penalty. Rejected.
- Restored the exact source blob `a4a487a0e1ddebe51f27d1af7eb90c98faf12e0c` from the previously CI-qualified streaming champion `d9680bd`; both experimental commits remain recoverable. These are CI-runner estimates, not official six-core scoring-box results.

## Physical-core worker cap and full-score proxy, October 31

- [CI 41199897294](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41199897294) passed official hashes, seed-2/3 differential, adversarial ordering, sanitizers and offline Docker. On its four-vCPU runner, 31 randomized full-input paired medians for forced 4/6/8/12 workers were 0.051952/0.055475/0.056512/0.059302 s, with exact SHA-256 on every run. Three-run maximum Docker cgroup peaks were 5,861,376/9,363,456/10,403,840/12,697,600 bytes. This is not six-core scoring-box data.
- The scoring host exposes twelve logical threads on six physical P-cores. Default workers are now capped at six, while SD_THREADS remains an override; the one-line source change leaves counted source lines and dependencies unchanged.
- [CI 41200171027](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41200171027) passed all correctness, seed, ASan/UBSan, TSan, Docker allowlist, offline-build and restricted-runtime gates for the new default. Paired 31-run full medians were 0.041953 s for the previous champion and 0.041843 s for the candidate (0.26% faster).
- Five-run cgroup peaks for the previous champion were 7,045,120/14,061,568/6,397,952/6,926,336/5,750,784 bytes; candidate 6,131,712/5,844,992/7,516,160/6,135,808/6,152,192 bytes. Medians improve 6.93 to 6.14 MB; the prior 14.06 MB maximum is an outlier, so maximum-based gains must not be treated as stable.
- With unchanged S/D, paired median T and median M imply about **+2.6% CI-proxy K**. This is not an official six-P-core K measurement; the 4/6/12-worker tradeoff should be checked on scoring hardware before final integration. Previous exact source remains recoverable at `d9680bd`.

## 32-bit output formatting and local negative controls, October 31

- Candidate `3524f05` used 32-bit `to_chars` for every output field, retaining 64-bit intermediate arithmetic for error ppm. Independently verified 2,000,000 random/boundary integers and 1,000,000 error-rate calculations against the original 64-bit formatter.
- [CI 41203844469](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41203844469) passed official seed-1 tiny/sample/full hashes, seed-2/3 reference differential, reordered input, ASan/UBSan, TSan, and restricted offline Docker.
- Direct 31-run full-input medians on four-vCPU CI: qualified six-worker champion **0.042887 s**, candidate **0.043311 s** (0.99% slower). Five-run maximum cgroup peaks **6,283,264** versus **6,602,752 bytes** (5.08% higher). Estimated K proxy approximately **1.4% lower**, even with one fewer source line and unchanged D. Rejected.
- Local isolated microbenchmarks did not justify promoting branchy 32-byte endpoint comparisons, grammar-branch method scanning, custom decimal-pair formatting, or alternative non-CRC hashes. The isolated 32-bit formatter was slightly faster but lost end-to-end.
- Restored the exact qualified six-worker source blob `ea7cdf2a960168dd13a729baedca7ad3b990dde6` from `f1e501c`. The CI workflow now compares future candidates directly against the verified six-worker implementation `ff20736`. Official six-core score remains outstanding.

## Short-endpoint SIMD and lazy delimiter scan, October 31

- Candidate `b6ce5ba` compares only the first AVX2 vector for endpoint keys shorter than 32 bytes, and loads the second delimiter vector only if the first cannot contain the endpoint terminator. Full endpoint equality remains exact.
- Independent 200,000-case randomized endpoint comparisons passed a length-aware bytewise oracle under ASan/UBSan, including prefix and altered-length mismatches. A separate 200,000-record mixed-method/endpoint scan agreed with the previous delimiter algorithm under ASan/UBSan.
- Local native microbenchmarks showed approximately 3-5% faster endpoint equality in the tested mixture; delimiter-scan timings were noisy and varied by short-endpoint share. These are isolated kernel measurements, not full-input gains.
- **Pending:** official seed-1 hashes, holdout-like seeds, restricted Docker, full paired T/M/S/D and CI result. Do not integrate this candidate as a verified champion until those gates pass. The qualified six-worker source remains at `817b13e` / `ff20736`.

## Short-endpoint SIMD CI decision, October 31

- [CI 41208670422](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41208670422) passed official seed-1 tiny/sample/full hashes, seed-2/3 differential, adversarial ordering, ASan/UBSan, TSan and restricted offline Docker for candidate `b6ce5ba`.
- Direct randomized 31-run full-input medians: six-worker champion `ff20736` **0.044322 s**, packed-only control **0.044519 s**, SIMD/lazy-scan candidate **0.049159 s**, **10.91% slower** than champion.
- Five fresh Docker cgroup memory.peak samples: champion 15,233,024 / 5,906,432 / 5,890,048 / 5,914,624 / 6,881,280 bytes; candidate 5,738,496 / 5,541,888 / 6,631,424 / 6,721,536 / 5,562,368 bytes. Champion's first-run 15.2 MB is an outlier; median 5.91 MB versus candidate 5.74 MB. Candidate loses estimated K by roughly 5%, even before any source penalty.
- **Rejected** SIMD/lazy-scan candidate; restored exact six-worker source blob `ea7cdf2a960168dd13a729baedca7ad3b990dde6`. The candidate is recoverable at `b6ce5ba`. Official six-P-core scoring remains outstanding.

## Short-key isolation and small-group crossover, October 31

- Isolated the short-key AVX2 equality branch in candidate `ccfc861`. On a local GCC 14.2 x86-64-v3 synthetic 3.1M-lookup Zipf mixture of 1,842 keys, 30 randomized trials gave old 17.427 ms versus candidate 25.159 ms median. This is an isolated synthetic benchmark, not a scored full-input comparison. Rejected the candidate and restored exact source blob `ea7cdf2a960168dd13a729baedca7ad3b990dde6` at `0eea733`.
- Compared one full `std::sort` for groups of size at most 16 against three exact selections. Local 41-trial synthetic workload (131,008 groups, 3,090,598 latencies) gave selection-only medians 15.362 ms original versus 14.601 ms threshold-16, about 5.0% faster. All tested percentile checksums agreed; this does not replace official oracle or sanitizer validation.
- Threshold-16 candidate `3e5a945` is **unqualified** pending official seed hashes, Docker/sanitizers and paired full-input T/M/S/D. Prior verified six-worker champion `ff20736` remains the integration recommendation until those gates pass. No official scoring-box result claimed.

## Official promotion — 2026-10-31

- The streaming hour-epoch source blob `2d60fab7bc533d01c31167ca0cd5d66216013ec9` was promoted to `sd-sm` commit `8298e2cbe0d21ffd4688bb23b625f988fdd4128d`. Official run `run-b447dc4b353e` passed all five gates with T=0.020573 s, M=21,041,152 B, S=441, D=1, K=46.641863 and 83.438795 points. This beats the prior BEST `run-2e35ec2bc727` (K=44.826321) by 4.05%.
- Official evidence and rollback state are recorded in the submission repository; the prior dev main source remains recoverable in its Git history.

## Concurrent dictionary race rollback — October 31

- Official-promoted source blob `2d60fab7bc533d01c31167ca0cd5d66216013ec9` uses non-atomic AVX2 tag reads concurrent with release stores. [CI 41218843179](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41218843179) and [CI 41219011089](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41219011089) failed ThreadSanitizer: write in `Names::add` versus non-atomic vector read in `Names::lookup`. Official output hashes do not establish race freedom.
- Restored exact qualified threshold-16 candidate source blob `f364d2123e1fde3869ab6a79fc6e0c85ba5aa085` from `3e5a945`: acquire-load dictionary tags, six-byte record copies, initialized spinlock and six-worker default. [CI 41217509351](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41217509351) passed official seed-1 hashes, seed-2/3, disorder, ASan/UBSan, TSan and restricted Docker.
- Paired 31-run full-input CI medians: six-worker champion 0.037612 s, threshold-16 candidate 0.037453 s (0.42% faster); five-run maximum cgroup peaks 8,347,648 and 7,045,120 bytes. This is a CI proxy, not an official six-core K. The previously official-promoted source scored K=46.641863, but its race requires correction before further promotion.
- Luna controls official submission. Do not integrate/re-promote the racing blob; use this qualified rollback for independent scoring-box verification and official rerun. This dev commit does not modify the submission repository.

## Race-free SIMD dictionary and scoring-host SMT capacity — October 31

- Candidate `c507060` replaces eight scalar acquire-load tag probes with four aligned 64-bit atomic pair loads and one AVX2 eight-lane hash comparison. The single insertion writer publishes each complete tag pair with release semantics; readers use acquire loads and compare endpoint bytes exactly. Unlike withdrawn vector tag reads, this does not perform non-atomic reads of concurrently modified tags.
- [CI 41227766850](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41227766850) passed seed-1 tiny/sample/full hashes, seed-2/3 reference differential, disorder, ASan/UBSan, TSan, restricted Docker and full-input paired comparisons. On four-vCPU CI, 31-run medians were 0.052288 s for `ff20736` versus 0.049866 s for the atomic-pair candidate (4.63% faster); five-run maximum cgroup peaks were 6,213,632 versus 7,450,624 bytes. These memory samples are noisy and do not establish a full-K win.
- Official [run-c4f4c1b69380](https://github.com/insightlabs38-pixel/sd-sm/commit/c4cb49fedd910dd65dc0719c113b06fa2e9c2476) had previously rejected the race-free threshold-16 variant with a six-worker cap: T=0.027990 s, M=16,519,168 B, K=41.885064. The older 12-logical-worker source scored K=44.826321 at its correctness-qualified BEST. These are different implementations, not an isolated worker-count comparison.
- Candidate `b45062c` restores up to twelve logical workers on the six-P-core/twelve-SMT scoring host while retaining the race-free atomic-pair dictionary and threshold-16 sorting. [CI 41227986546](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41227986546) passed all gates; four-vCPU 31-run medians 0.051800 s for `ff20736` versus 0.049822 s for candidate (3.82% faster), max-five cgroup peaks 6,381,568 versus 6,754,304 bytes. Four-vCPU CI cannot validate twelve-worker scoring-host behavior.
- **Status: CI-qualified challenger, not an official champion.** Luna should compare this exact source on the official six-P-core host against the correctness-qualified BEST, preserve rollback, and promote only on measured complete K plus correctness. No official result is claimed here.
## Race-free SIMD dictionary and CPU diet, October 31 (Claude frontier session, round 2)

- **Race repair.** The racing blob `2d60fab` read a bucket's tag vector while another worker published a tag. Now each worker owns a private open-addressing table (16K x uint16 ids, hash = CRC32 of length + first/last 8 bytes) and every hit is confirmed by a full 128-byte masked key compare. The shared part is an append-only log (key image, length, fingerprint) written under a lock and published by a release store of its size; workers acquire-load the size and mirror new entries into their table. Nothing a worker reads is ever written concurrently, so there is no race to excuse. ThreadSanitizer is clean at 6 and 12 threads (repeated), and still flags the racy blob in the same harness.
- **Selection.** Values are held as int32 with the sign bit flipped. An out-of-place AVX2 partition (permute tables, 8 lanes per step) drives a multi-rank recursion that finds p50/p95/p99 together; leaves of at most 16 values use a bitonic network. Single-thread select time 49 -> 22 Mcycles.
- **Other.** Software prefetch 1280 bytes ahead per line (parse 46 -> 39 cycles/line), branchless 8-digit formatter, live-endpoint list instead of a skip branch, parse loop without per-line range checks, `_exit` after the last write, default workers = logical CPUs (cap 16).
- **Local evidence (4 vCPU Xeon, not the scoring box).** Seed-1 full, interleaved: main `961cf36` 0.052-0.060 s wall / 180 ms CPU; this build 0.036-0.039 s / 115 ms CPU. cgroup v1 peak with 4 workers 6-8 MB, 12 workers 13-15 MB. Seeds 1-3 full outputs match the reference.
- **Rejected.** pread into per-thread buffers (+27 ms sys), MADV_POPULATE_READ per block (slower), branchless full-width key compare (7% slower), 8K/4K-slot private tables (no gain), dropping fadvise/sync_file_range (4% faster, +10 MB peak), smaller kAhead (no M change).
- Rollback: `961cf36` (race-free, K=41.9 official), `2d60fab` (racy, not promotable).

### Follow-up measurements (same session)

- Lookahead bounded to two blocks per worker (was 48 blocks): 12-worker cgroup peak mean of 12 runs 15 -> 12 MB, time unchanged. Peak varies 9-23 MB between runs on the 4 vCPU box, mostly with output writeback timing and thread preemption, so single peaks say little.
- Exit: workers release the input mapping in shared 4 MiB steps instead of fixed per-thread slices, so workers that are idle during the last hours absorb the zap cost; early block-group zapping (8/16/32 blocks) was slower (TLB shootdowns), as before.
- Neutral, not kept: capping concurrent finalizations (3/5), a window on out-of-order hours (5/8), 128/512 KiB blocks (128 KiB +10% CPU), 2/3/4/6 interleaved streams, `-mtune`/`-funroll-loops`/`-O2`, skipping writeback calls for the last hour, fully unrolled stream loop (simulated mispredicts -40%, wall unchanged).
- Output writeback syscalls (`write` + `sync_file_range` + `fadvise`) cost about 20 ms CPU of 115 but keep peak memory ~10 MB lower; O_DIRECT was not tried because a slow `/out` device would make every write synchronous.
- Source: about 565 canonical code lines (clang-format LLVM, comments/blanks removed) versus 440 for `961cf36`; the CPU saving is worth roughly 25x the S penalty in K.
