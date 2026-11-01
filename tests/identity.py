"""Full-byte identity under deliberately equal endpoint fingerprints and disorder."""
import collections
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile

binary = sys.argv[1]
rng = random.Random(11012026)
# All 1,842 names have identical length, first eight and last eight bytes.
# The champion's CRC input is therefore identical for every distinct name.
names = [f'/same___/key{i:06d}/abcdefghijklmnopqrstuvwxyz/tail___' for i in range(1842)]
values = [0, 1, 1048574, 1048575, 1048576, 16777215, 16777216, 2147483647, 2147483648, 4294967295]
groups = collections.defaultdict(list)
lines = []
from datetime import datetime, timedelta
start = datetime(2026, 10, 30, 18)
for i in range(20000):
    hour = (i * 37) % 72
    name = names[i % len(names)]
    latency = values[i % len(values)]
    status = [100, 200, 499, 500, 599][i % 5]
    stamp = (start + timedelta(hours=hour)).strftime('%Y-%m-%dT%H:00:00')
    lines.append(f'{stamp}.000000Z GET {name} {status} {latency}\n')
    groups[(stamp + 'Z', name)].append((latency, status >= 500))
expected = []
for (hour, name), items in sorted(groups.items()):
    latencies = sorted(x[0] for x in items)
    n = len(items); errors = sum(x[1] for x in items)
    ranks = [latencies[(p * n + 99) // 100 - 1] for p in (50, 95, 99)]
    expected.append(f'{hour} {name} {n} {ranks[0]} {ranks[1]} {ranks[2]} {errors} {errors * 1000000 // n}\n')
expected = ''.join(expected).encode()
with tempfile.TemporaryDirectory() as tmp:
    data = Path(tmp) / 'data'; out = Path(tmp) / 'out'; data.mkdir(); out.mkdir()
    for order in ('sorted', 'shuffled'):
        if order == 'sorted': lines.sort()
        else: rng.shuffle(lines)
        (data / 'requests.log').write_text(''.join(lines))
        for threads in (1, 12):
            env = dict(os.environ, SD_THREADS=str(threads), SD_DATA_DIR=str(data), SD_OUT_DIR=str(out))
            subprocess.run([binary], env=env, check=True)
            assert (out / 'summary.txt').read_bytes() == expected, (order, threads)
            print(f'PASS equal-CRC identity {order} threads={threads} groups={len(groups)}', flush=True)
