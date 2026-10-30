# Track A: Ingest

Dependency-free C++20 implementation of the [Speed Demon 2026 Track A specification](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

Five newline-aligned mmap partitions are parsed concurrently. Each worker uses a compact, full-key-checked endpoint dictionary and exact per-hour counters, then emits packed group/latency records. After the workers join, endpoint IDs are reconciled by full-string equality, counts are merged, and latencies are scattered into contiguous group spans. Five output workers independently compute exact nearest-rank percentiles and serialize disjoint hour ranges in deterministic byte order.

## Dependencies

No third-party runtime libraries. Build with C++20, pthreads and the pinned Docker toolchain. The default input is `/data/requests.log`, and the only output is `/out/summary.txt`. `SD_DATA_DIR` and `SD_OUT_DIR` optionally override these directories for local testing.

## With another 24 hours

Profile on the official scoring hardware, tune parse/output worker counts, compare lower-overhead group remapping and exact selection kernels, and run broader heldout-seed and Docker sandbox tests. Preserve a verified rollback.
