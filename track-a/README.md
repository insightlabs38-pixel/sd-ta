# Track A: Ingest

A dependency-free C++20 implementation of the [Speed Demon 2026 Track A specification](https://github.com/hackathon-raptors/speed-demon-2026/blob/main/specs/a-ingest.md).

## Approach

A single mmap scan parses the fixed UTC timestamp and fields, interns complete endpoint strings, and accumulates per-group counts and errors. Compact `(group, latency)` records are scattered into contiguous group spans, then exact nearest-rank percentiles are computed by sorting small groups or partitioning large ones. Output uses bytewise endpoint ordering and integer-only formatting.

## Dependencies

No third-party runtime libraries. Build with C++20 and the pinned Docker toolchain. The default input is `/data/requests.log`, and the only output is `/out/summary.txt`. `SD_DATA_DIR` and `SD_OUT_DIR` optionally override these directories for local testing.

## With another 24 hours

Profile official full-scale runs, compare direct-to-span two-pass parsing and worker-local grouping, and test alternative percentile selection kernels against complete time/memory/source/dependency scoring. Preserve exactness on unseen seeds.
