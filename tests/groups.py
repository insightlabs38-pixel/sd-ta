"""Large single-group latency patterns that stress selection: duplicates, sorted, organ-pipe, few values."""
import os
import random
import subprocess
import sys
import tempfile

rng = random.Random(5)
N = 60000
patterns = {
    'equal': [77] * N,
    'binary': [rng.randrange(2) for _ in range(N)],
    'ascending': list(range(N)),
    'descending': list(range(N, 0, -1)),
    'organ-pipe': list(range(N // 2)) + list(range(N // 2, 0, -1)),
    'few-values': [rng.choice((0, 1, 4294967295)) for _ in range(N)],
    'heavy-tail': [int(rng.paretovariate(1.1) * 100) % 4294967296 for _ in range(N)],
    'sawtooth': [i % 97 for i in range(N)],
}
for name, values in patterns.items():
    rows = [f'2026-10-30T18:{i % 60:02d}:00.000000Z GET /v2/g {500 if i % 9 == 0 else 200} {v}\n' for i, v in enumerate(values)]
    ordered = sorted(values)
    n = len(ordered)
    pick = lambda q: ordered[(q * n + 99) // 100 - 1]
    errors = sum(1 for i in range(n) if i % 9 == 0)
    want = f'2026-10-30T18:00:00Z /v2/g {n} {pick(50)} {pick(95)} {pick(99)} {errors} {errors * 1000000 // n}\n'
    with tempfile.TemporaryDirectory() as d:
        os.mkdir(d + '/data')
        os.mkdir(d + '/out')
        with open(d + '/data/requests.log', 'w') as f:
            f.write(''.join(rows))
        subprocess.run([sys.argv[1]], env=dict(os.environ, SD_DATA_DIR=d + '/data', SD_OUT_DIR=d + '/out'), check=True)
        with open(d + '/out/summary.txt') as f:
            assert f.read() == want, name
    print('PASS group pattern', name, flush=True)
