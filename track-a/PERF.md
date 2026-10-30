# Track A performance log

## Initial exact C++ baseline, October 30, 2026

- Design: single mmap parse, endpoint interning, count/scatter, exact local sorting or multiselect.
- Local compiler: GCC 14.2, `-O3 -std=c++20 -static-libstdc++ -static-libgcc`.
- Synthetic differential tests: 12 cases covering empty and small files, 1,000 to 310,000 records, four seeds, unsigned-32-bit latency extremes, duplicated lines, all status classes, month/hour boundaries, and endpoint ordering. All passed against a Python integer oracle.
- Local 310,000-record synthetic run: 0.05 s wall and 36,544 KiB peak RSS on the available development container. This is **not** an official benchmark or a scoring-box measurement.
- Official public seed-1 and holdout hashes: pending GitHub CI. No board run ID exists yet; Luna records actual board IDs after integration.
