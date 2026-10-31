# Track A: Ingest

Dependency-free C++20 implementation of the [Speed Demon 2026 Track A specification](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

The log is almost time-ordered, so the program never holds all records at once.

- **Blocks.** The memory-mapped file is cut into 256 KiB newline-aligned blocks. One worker per logical CPU claims blocks and parses each with four interleaved line streams: SIMD space scanning, software prefetch, SWAR/SSE integer decoding. A block yields 6-byte records (latency, endpoint id, 5xx bit) grouped into per-hour runs.
- **Dictionary.** Each worker owns an open-addressing table (CRC32 of length and first/last 8 bytes) and confirms every hit with a full 128-byte key compare, so identity is exact. New names go to a shared append-only log under a lock; its size is published with a release store and workers mirror new entries after an acquire load. Nothing a worker reads is written concurrently, so the lookups are plain SIMD reads and ThreadSanitizer is clean.
- **Hours.** Once the completed prefix of the file has reached two hours past the start of an hour, the hour is finalized by whichever worker is free: counting scatter into one array, exact nearest-rank p50/p95/p99 by an out-of-place AVX2 partition recursion (bitonic network for up to 16 values), branchless formatting. If a late line ever violates the assumption, the block that carries it is detected and the whole run is repeated without early finalization, so the output is exact for any input order.
- **Output.** The worker that completes the next ready hour writes consecutive ready hours in order, starts writeback and drops old page cache; no dedicated writer thread is needed.
- **Exit.** Idle workers release the file mapping in shared 4 MiB steps and the process leaves with `_exit`.

Measurements and rejected experiments are in `PERF.md`.

## Dependencies

No third-party runtime libraries. Build with C++20, pthreads and the pinned Docker toolchain. The default input is `/data/requests.log` and the only output is `/out/summary.txt`; `SD_DATA_DIR`, `SD_OUT_DIR` and `SD_THREADS` override them for local testing.
