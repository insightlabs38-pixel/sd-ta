# Track A: Ingest

Dependency-free C++20 implementation of the [Speed Demon 2026 Track A specification](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

The log is almost time-ordered, so the program never holds all records at once.

- **Blocks.** The memory-mapped file is cut into 256 KiB newline-aligned blocks. Every hardware thread claims blocks and parses them with four interleaved line streams: SIMD space scanning, exact endpoint interning, SWAR/SSE integer decoding. A block yields 6-byte records (latency, endpoint id, 5xx bit) grouped into per-hour runs.
- **Dictionary.** One shared endpoint table (8-way buckets, CRC32 tags, lock-free hits, locked inserts). A hit is confirmed by comparing the full key bytes, so identity is exact.
- **Hours.** Once the completed prefix of the file has reached two hours past the start of an hour, the hour is finalized by whichever thread is free: counting scatter into one array, exact nearest-rank selection (branchless Lomuto multi-select), formatting. If a late line ever violates the assumption, the block that carries it is detected and the whole run is repeated without early finalization, so the output is exact for any input order.
- **Output.** The worker that completes the next ready hour writes consecutive ready hours in order, starts writeback and drops old page cache; no dedicated writer thread is needed.
- **Exit.** Threads release their share of the file mapping in parallel so that process teardown is cheap.

Measurements and rejected experiments are in `PERF.md`.

## Dependencies

No third-party runtime libraries. Build with C++20, pthreads and the pinned Docker toolchain. The default input is `/data/requests.log` and the only output is `/out/summary.txt`; `SD_DATA_DIR`, `SD_OUT_DIR` and `SD_THREADS` override them for local testing.
