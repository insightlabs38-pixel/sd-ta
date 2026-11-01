#!/usr/bin/env python3
"""Prove lossless uint32 escape words across every tag, hour and ordering."""
import os
import random
import subprocess
import sys
import tempfile
from collections import defaultdict

rng = random.Random(61101)
names = [f"/v2/word4/{i:04d}" for i in range(1842)]
boundary = (0, 1, (1 << 20) - 2, (1 << 20) - 1, 1 << 20,
            (1 << 20) + 1, (1 << 31) - 1, 1 << 31, (1 << 32) - 1)
groups = defaultdict(list)
rows = []
for i in range(20000):
    # First 1842 records publish the whole catalogue, then exercise hot paths.
    ep = names[i] if i < len(names) else names[rng.randrange(len(names))]
    h = (i * 17) % 72
    value = boundary[(i // 3) % len(boundary)]
    if i % 4 == 0:
        value = (1 << 32) - 1  # every-run worst-case overflow traffic
    status = (200, 499, 500, 599)[i % 4]
    hour_day = h + 18
    d = ("2026-10-30", "2026-10-31", "2026-11-01", "2026-11-02")[hour_day // 24]
    hour_text = f"{d}T{hour_day % 24:02d}:00:00Z"
    line = f"{d}T{hour_day % 24:02d}:{i % 60:02d}:{(i * 3) % 60:02d}.000001Z GET {ep} {status} {value}\n"
    rows.append(line)
    groups[(hour_text, ep)].append((value, status >= 500))
expected = []
for (hour, name), samples in sorted(groups.items()):
    vals = sorted(v for v, _ in samples)
    n = len(vals)
    pick = lambda k: vals[(k * n + 99) // 100 - 1]
    errors = sum(int(is_error) for _, is_error in samples)
    expected.append(f"{hour} {name} {n} {pick(50)} {pick(95)} {pick(99)} {errors} {errors * 1000000 // n}\n")
with tempfile.TemporaryDirectory() as d:
    os.mkdir(d + "/data")
    os.mkdir(d + "/out")
    for order, content in (("ordered", rows), ("shuffled", rng.sample(rows, len(rows)))):
        with open(d + "/data/requests.log", "w") as f:
            f.writelines(content)
        env = dict(os.environ, SD_DATA_DIR=d + "/data", SD_OUT_DIR=d + "/out")
        subprocess.run([sys.argv[1]], check=True, env=env)
        got = open(d + "/out/summary.txt").read()
        assert got == "".join(expected), (order, len(got), len("".join(expected)))
        print("WORD4_ESCAPE_EXACT", order, len(rows), len(groups), flush=True)
