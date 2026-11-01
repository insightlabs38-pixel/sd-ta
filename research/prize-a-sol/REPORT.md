# Track A prize architecture evaluation — November 1, 2026

The strongest replicated challenger is `prize/a-sol-lean-pipeline`: exact shared dictionary publication, compact numeric completed hours, immediate recycling of scattered runs, and demand-sized latency buffers. Source blob **`1ad533f716e9f898eeb0e174959bfb25cad52e85`**, source commit `1961c14`, **S=578, D=1**. It is qualified for integrator evaluation, not promoted or submitted officially.

Two independent complete CI runs project **K=52.65 and 53.14** from the official BEST K=49.960098. This falls short of the requested 58–60 target. No official-score improvement is claimed. Main and `sd-sm` were preserved.

## Controls and measurement

The immutable control is repository commit `4aa987898640222750cd5ee68500c3f3b9967375`, source blob `c91b9689529c9c92472be91dbc36f0716e2c98e5`. Its official axes supplied for this task are T=0.016388 s, M=21,344,256 B, S=584 and D=1. All comparisons here use that source, not the older controls in historical CI.

Each qualification builds both complete executables through the same pinned Dockerfile: GCC 15 image digest `eb5e64286ad47187b32bda75c6b6916166ecb41da56559b8b09e0001748d2cce`, Debian runtime digest `62bd423aeee912639ca8ee681c2b762cb3e25137e0a92b20be455898a9f034ad`, identical `-O3 -march=x86-64-v3 -std=c++20 -DNDEBUG -pthread -static-libstdc++ -static-libgcc -s` flags and no network during build. Timing uses the extracted complete executables, with fresh output files, warmed input, two discarded warmups, 31 randomized pairs per full seed at 1/2/12 workers, and a 105 ms pause outside each timed interval. Thus cleanup of a previous summary is outside T, as in the official fresh-output contract. This is process-spawn wall time on GitHub hardware, not the official internal timer.

Memory uses 15 randomized fresh-container pairs per seed and worker count (2 and 12), restricted runtime settings and fresh output files; input is warmed before the run cgroup exists. The table below uses the geometric mean across three seeds of each seed's candidate/control **median**. It does not mix absolute timings or memory between runners. Twelve workers on a two-CPU GitHub runner do not reproduce twelve SMT contexts on six physical scoring cores.

The official harness reads raw `memory.peak`. Prewarming keeps input pages charged outside the run cgroup; it does **not** subtract all file cache from M. Output page-cache/writeback variation remains part of the measured peak. Receipt `end_anon`/`end_file` fields are post-solution snapshots and cannot attribute an earlier peak. RSS includes the mapped input and is not used as scored M. Medians, ranges, paired order and raw samples are retained; no maximum-only outlier is promoted into a score claim.

Canonical source counts use clang-format 23.1.3, LLVM defaults, and scc 4.1.0 (release archive SHA-256 verified), including the eight Dockerfile code lines. This exactly reproduces S=584 for the champion. No dependency was added: D=1 for every candidate.

## Architectural experiments

| Branch suffix | Exact source blob | S | 12-worker T ratio | M ratio | Projected K | Decision |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `input-references` | `2d4561060c1d71591e82803876239e90bbb6e006` | 592 | 1.0099 | 0.9989 | 49.63 | Reject |
| `ordered-writer` | `5e21597807f098b757b951228cab443f37f41d02` | 568 | 1.0776 | 1.2910 | 45.92 | Reject |
| `binary-hours` | `b1dafb152180928b5861e272be40ace3e9c4bcbb` | 595 | 0.9892 | 0.8775 | 51.42 | Retain alternative |
| `shared-index` | `e6102c18d66f60a721d34e296265e3a23ebb464a` | 565 | 0.9586 | 0.9895 | 51.39 | Retain simpler alternative |
| `lean-pipeline`, first | `1ad533f716e9f898eeb0e174959bfb25cad52e85` | 578 | 0.9474 | 0.8877 | 52.65 | Strongest replicated challenger |
| `lean-pipeline`, replication | same | 578 | 0.9489 | 0.8441 | 53.14 | Confirms direction |
| `endpoint-chains` | `8aa3567292372a6dd624902a3fd3c43a95509687` | 584 | 1.2549 | 1.1000 | 43.76 | Reject |
| `writer-pipeline` | `3aa7f9c4de26d9839790467f812f677bad8a900e` | 582 | 0.9598 | 0.8657 | 52.52 | Retain; below replicated lean result |

All branch names begin `prize/a-sol-`. The source commits and full blob IDs are also in [source-manifest.json](source-manifest.json). None of the alternatives replaces the champion automatically.

* **Input references:** four bytes encode a block-relative address of the latency text and the endpoint/error tag. Numeric decoding moves to finalization; all uint32 values need no escapes or sidecars. This meaningfully differs from rejected PR #5. Exact mapping lifetime is extended until finalization completes. Reduced record width did not produce a robust 12-worker memory or K gain; local GCC 14/15 runs were also unfavorable at twelve workers. Reject despite better single-worker CI timing.
* **Ordered backpressure:** each finalizer retains its text until its write turn, removing the ready-hour queue and shared output-buffer pool. It passes exactness, but waiting finalizers retain other workspace and obstruct the pipeline. Both T and M regress, outweighing sixteen saved source lines.
* **Compact numeric hours:** finalizers publish count, three quantiles, error count and endpoint ID in 24 bytes per active group. Only the active writer expands them into ASCII through one text buffer. This attacks the retained-text lifetime, rather than changing eviction distances or output syscall batching. At twelve workers it lowers median peaks across all three full seeds while preserving throughput. At two workers memory is about 5% worse, so this is not a universal memory reduction.
* **Shared exact index:** one open-addressed atomic slot array replaces worker-local tables, append-log mirroring, cached fingerprints and synchronization cursors. A slot's release store publishes an immutable key image and length; its acquire load precedes a complete byte comparison. A lock protects insertion and sorted IDs. Collisions never establish identity; no saturation certificate or public-seed assumption is used. This removes startup replication and nineteen scored lines, with a repeated multi-seed runtime gain.
* **Combined lean pipeline:** combines the preceding useful mechanisms, returns run buffers immediately after scatter (before selection/output), and removes the unconditional 512 KiB latency reserve. Record format, SIMD selector and disorder fallback remain exact. This preserves the champion's fast parsing machinery while changing ownership and retention.
* **Endpoint chains:** parser-built per-endpoint chains replace tag counting/scatter and the whole-hour latency array. Each six-byte node retains a full uint32 latency and a bounded chunk-local link. Gathering one endpoint at a time is exact, but header initialization, append bookkeeping and pointer traversal cost much more than the eliminated scatter. Full CI confirms the loss. This was tested after the first promising combined result; it was not rejected merely by analogy with previous experiments.

A seventh design, `writer-pipeline`, separates numeric-result production from a dedicated ordered consumer. [CI 41333415728](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41333415728) passed every exactness, collision, strict-sanitizer and Docker gate. Twelve-worker T/M ratios are 0.9598/0.8657, S=582, D=1, projecting K=52.52. It does not beat the replicated lean result. It was pursued after the combined design succeeded locally; worker/output coordination was not assumed optimal.

## Replicated strongest-candidate axes

T is seconds, M is bytes. Each entry is control → challenger on that same runner; all timings are medians of 31 fresh-output matched trials, memory medians of 15 fresh-container matched trials.

| CI | Seed | T at 12 workers | M at 12 workers |
| --- | ---: | --- | --- |
| [41332666362](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41332666362) | 1 | 0.043562786 → 0.041250552 | 15,540,224 → 14,057,472 |
| same | 2 | 0.042261144 → 0.040064917 | 15,556,608 → 14,233,600 |
| same | 3 | 0.043028755 → 0.040761263 | 15,781,888 → 13,340,672 |
| [41333014119](https://github.com/insightlabs38-pixel/sd-ta/actions/runs/41333014119) | 1 | 0.047722327 → 0.045496274 | 11,862,016 → 10,145,792 |
| same | 2 | 0.047623758 → 0.045131993 | 12,795,904 → 10,309,632 |
| same | 3 | 0.048072459 → 0.045459216 | 11,436,032 → 9,981,952 |

The first run's baseline peaks ranged from 12,849,152 to 17,993,728 B across seeds/trials, and challenger peaks from 12,718,080 to 15,900,672 B. This variability is substantial. Local supplemental GCC-15 runtime-container measurements, randomized nine pairs per full seed at twelve workers, independently gave median peaks 12.947/12.857/13.472 MB for the champion versus 10.486/11.268/10.490 MB for the lean pipeline. These local binaries use the extracted pinned compiler but host headers; the full Docker-built CI comparisons are the qualification evidence.

The proportional projection is:

`K_new = 49.960098 × (T_new/T_old)^(-0.50) × (M_new/M_old)^(-0.20) × (584/S_new)^(0.15) × (D_new/1)^(0.15)`.

D is 1 throughout, so its multiplier is 1. For the first lean run this projects T≈0.015526 s, M≈18.95 MB and K≈52.65; replication projects K≈53.14. These transfer CI ratios onto official axes; they are not official measurements or confidence bounds. An additive-byte memory transfer is more conservative because official memory includes different fixed/kernel costs. Neither model establishes a prize-winning result. A 58–60 score still requires a substantially larger source, runtime or memory improvement.

## Phase-level profiling and allocation attribution

Supplemental local GCC-15 profiling uses `CLOCK_THREAD_CPUTIME_ID`, seven interleaved full-seed-1 runs per implementation and worker count. Medians at twelve workers:

| Worker CPU subset / retained capacity | Champion | Lean pipeline |
| --- | ---: | ---: |
| Parse CPU sum | 68.91 ms | 62.22 ms |
| Finalize CPU sum, including emission | 42.57 ms | 40.42 ms |
| Emission CPU subset | 7.79 ms | 14.67 ms |
| Retained run-buffer capacity | 4.14 MB | 3.86 MB |
| Retained latency-vector capacity | 3.15 MB | 1.73 MB |
| Text-buffer allocation capacity | 23.60 MB | 0.52 MB |
| Numeric-result allocation capacity | — | 2.46 MB |
| Selector scratch capacity | 0.55 MB | 0.54 MB |

Phases overlap across workers; emission is included in finalization and must not be added twice. Formatting moved into emission, explaining the larger emission subset. Timers and allocation counters perturb execution, so qualification uses uninstrumented binaries. Capacities are requested/retained storage, not resident memory or simultaneous peak attribution: most of each former 512 KiB text buffer was untouched. The observed M reduction is 11–16% on CI, not the much larger reduction in virtual text capacity.

The shared key images occupy about 256 KiB, with length metadata and one 32 KiB slot array. The old twelve worker-local arrays additionally occupied 384 KiB plus their allocation/initialization costs. Input mapping pages are deliberately not confused with anonymous workspace. Output file cache, libc arenas, thread stacks and kernel charges account for remaining differences; post-exit `memory.stat` cannot partition their peak exactly.

Recompute every projection with `python research/prize-a-sol/analyze_receipts.py`. [qualification-gates.json](receipts/qualification-gates.json) records successful steps, exact workflow commits and URLs for all eight complete qualification runs. [source-manifest.json](source-manifest.json) maps each source commit to its exact blob and scored axes.

## Correctness and reproducibility

Both lean qualifications passed official seed-1 tiny/sample/full SHA-256s, independent reference comparison for seeds 2/3 samples, randomized synthetic oracles, endpoint lengths 1–125, dense minimum-length records, percentile/group-size and uint32 latency boundaries, and reordered official samples at seeds 4/5 (original, reversed, sorted, shuffled). The exact fallback for unbounded disorder is retained.

The second qualification additionally runs [tests/identity.py](../../tests/identity.py): 1,842 different equal-length names share identical first/last eight bytes, deliberately giving **identical CRC input**. Twenty thousand rows cover hour/month boundaries, 499/500/599, zero, compression-boundary values and UINT32_MAX, with sorted/shuffled orders at 1/12 workers against an independent Python oracle. It runs normally, under ASan/UBSan with halt/abort-on-error, and under TSan with halt-on-error. This tests full-byte identity and immutable publication rather than probabilistic uniqueness.

Expected full-output SHA-256s, independently matched by every tested challenger:

* seed 1: `495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3`
* seed 2: `fe3469ff108066bd7b65f8239bbe27615467b99db182b79919c572c3d2910127`
* seed 3: `5207056c6c7479374bc695261be9c4d37e13f2e49ece1f27669694ab122bcb24`

All qualification jobs also validate the Docker allowlist, pinned offline build and restricted tiny/full container output. Source changes are isolated from `main` and `sd-sm`. Official submission experiments remain exclusively with the integrator.

The [receipts](receipts/) retain individual timing and memory samples, local controls, profiling measurements and source manifests. GitHub's full original artifacts also retain complete `memory.stat` fields. The `prize/a-sol-investigation` evidence-export workflow copies compact numeric receipts into accessible job logs; it does not change or rerun scored code. Initial failed workflow attempts were comparator-path errors after all correctness/build gates passed; the table uses the corrected successful runs only.

Historical evidence reviewed includes PRs #4/#5/#6, Round 3 writeback/output/page-cache accounting, failed hour-owner/radix architectures, exact dictionary caches and PGO research. None was promoted again on an isolated benchmark result.

## Integrator handoff

Evaluate the exact lean blob above first, preserving `c91b968` as the qualified fallback. Measure complete official T/M/S/D with fresh output directories and repeated matched controls; keep it only if official K improves. Retain `shared-index` as the smaller, throughput-oriented alternative and `binary-hours` as the isolated memory mechanism. The local and CI evidence supports an official trial, not a merge based solely on projected K. No manual code-quality score was assigned or assumed.
