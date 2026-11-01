#!/usr/bin/env python3
"""Adversarial 1842-key saturation test: ambiguous partial CRC signatures."""
import datetime as dt
import os
import pathlib
import subprocess
import sys
import tempfile
from collections import defaultdict

base = dt.datetime(2026, 10, 30, 18)
names = [f"/unique/path/{i:04d}/{i*i:08d}" for i in range(1800)]
# All 42 below have equal length, first eight and last eight bytes,
# deliberately forcing equal CRC32(first8,last8,length) fingerprints.
names += [f"/catalog/{i:04d}/constant" for i in range(42)]
assert len(set(names)) == 1842
records = []
for i, ep in enumerate(names):
    records.append((i % 72, ep, i % 3, (0, 4294967295, 1600)[i % 3]))
for i in range(1200):
    ep = names[(i * 1597 + 23) % len(names)]
    records.append(((i * 13) % 72, ep, (i // 3) % 3,
                    (4294967295, 0, 10000 + i)[i % 3]))
groups = defaultdict(lambda: [[], 0])
with tempfile.TemporaryDirectory() as td:
    data = pathlib.Path(td) / "data"
    out = pathlib.Path(td) / "out"
    data.mkdir()
    out.mkdir()
    with (data / "requests.log").open("w") as f:
        for j, (hour, ep, stat, lat) in enumerate(records):
            timestamp = (base + dt.timedelta(hours=hour,seconds=j % 3600)).strftime("%Y-%m-%dT%H:%M:%S") + f".{j%1000000:06d}Z"
            status = (499, 500, 599)[stat]
            f.write(f"{timestamp} GET {ep} {status} {lat}\n")
            a = groups[hour,ep]
            a[0].append(lat)
            a[1] += status >= 500
    expected = []
    for (hour,ep),(latencies,errors) in sorted(groups.items()):
        latencies.sort()
        n = len(latencies)
        q = lambda p: latencies[(p*n+99)//100-1]
        timestamp = (base+dt.timedelta(hours=hour)).strftime("%Y-%m-%dT%H:00:00Z")
        expected.append(f"{timestamp} {ep} {n} {q(50)} {q(95)} {q(99)} {errors} {errors*1000000//n}\n")
    expected = "".join(expected).encode()
    for workers in (1, 4, 12):
        subprocess.run([sys.argv[1]], env=dict(os.environ,
                       SD_THREADS=str(workers), SD_DATA_DIR=str(data),
                       SD_OUT_DIR=str(out)), check=True)
        actual = (out / "summary.txt").read_bytes()
        if actual != expected:
            raise AssertionError(f"catalog saturation disagreement: threads={workers}, actual={len(actual)}, expected={len(expected)}")
    print("CATALOG_CERTIFICATE_EXACT threads=1,4,12 records=3042 distinct=1842")
