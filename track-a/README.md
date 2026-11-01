# Track A: Ingest

Dependency-free C++20 implementation of the [Speed Demon 2026 Track A specification](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

The log is almost time-ordered, so the program never holds all records at once.

- **Blocks.** The memory-mapped file is cut into 256 KiB newline-aligned blocks. One worker per logical CPU claims blocks and parses each with four interleaved line streams: SIMD space scanning, software prefetch, SWAR/SSE integer decoding. A block yields 6-byte records (latency, endpoint id, 5xx bit) grouped into per-hour runs.
- **Dictionary.** One shared open-addressing table publishes immutable endpoint images through release/acquire atomic slots. Every hit checks the complete endpoint bytes. Inserts and the byte-ordered ID list use a lock; there is no per-worker table or mirroring log.
- **Hours.** Once the completed prefix of the file has reached two hours past the start of an hour, the hour is finalized by whichever worker is free: counting scatter into one array, exact nearest-rank p50/p95/p99 by an out-of-place AVX2 partition recursion (bitonic network for up to 16 values), branchless formatting. If a late line ever violates the assumption, the block that carries it is detected and the whole run is repeated without early finalization, so the output is exact for any input order.
- **Output.** Completed hours wait as 24-byte numeric results per active endpoint. The worker consuming ready hours formats them through one shared text buffer, writes them in order, starts writeback and drops old page cache. Consumed input runs return to their pool immediately after scatter.
- **Exit.** Idle workers release the file mapping in shared 4 MiB steps and the process leaves with `_exit`.

Measurements and rejected experiments are in `PERF.md` and [the prize architecture report](../research/prize-a-sol/REPORT.md).

## Dependencies

No third-party runtime libraries. Build with C++20, pthreads and the pinned Docker toolchain. The default input is `/data/requests.log` and the only output is `/out/summary.txt`; `SD_DATA_DIR`, `SD_OUT_DIR` and `SD_THREADS` override them for local testing.
