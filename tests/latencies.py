"""Every latency digit count 1..10, powers of ten and their neighbours, in one hour of one endpoint."""
import os
import subprocess
import sys
import tempfile

values = sorted({v for k in range(10) for v in (10 ** k - 1, 10 ** k, 10 ** k + 1)} | {4294967295, 2 ** 31, 2 ** 24, 99999999, 100000000, 123456789})
values = [v for v in values if v <= 4294967295]
rows = []
for i, v in enumerate(values * 3):
    rows.append(f'2026-10-30T18:{i % 60:02d}:00.000000Z GET /v2/lat {500 if i % 4 == 0 else 200} {v}\n')
expected = sorted(v for v in values * 3)
n = len(expected)
p = lambda q: expected[(q * n + 99) // 100 - 1]
errors = sum(1 for i in range(n) if i % 4 == 0)
want = f'2026-10-30T18:00:00Z /v2/lat {n} {p(50)} {p(95)} {p(99)} {errors} {errors * 1000000 // n}\n'
with tempfile.TemporaryDirectory() as d:
    os.mkdir(d + '/data')
    os.mkdir(d + '/out')
    open(d + '/data/requests.log', 'w').write(''.join(rows))
    subprocess.run([sys.argv[1]], env=dict(os.environ, SD_DATA_DIR=d + '/data', SD_OUT_DIR=d + '/out'), check=True)
    got = open(d + '/out/summary.txt').read()
    assert got == want, (got, want)
print('PASS latencies', n, 'rows')
