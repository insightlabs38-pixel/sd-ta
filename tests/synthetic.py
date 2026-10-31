"""Differential smoke tests using legal Track A grammar and a Python oracle."""
import datetime as dt
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
from collections import defaultdict

BASE = dt.datetime(2026, 10, 30, 18)
METHODS = ('GET', 'POST', 'PUT', 'PATCH', 'DELETE', 'HEAD', 'OPTIONS')
ENDPOINTS = ['/', '/health', '/healthz', '/v2/checkout', '/v2/orders',
             '/v2/orders-export', '/v2/orders.csv', '/v2/orders/{id}',
             '/v2/orders/{id}/items', '/v2/ordersByDate', '/v2/orders_legacy',
             '/v2/orderstatus',
             '/internal/v1/tenants/{tenant_id}/projects/{project_id}/pipelines/{pipeline_id}/runs/{run_id}/artifacts/{artifact_id}:download']
ENDPOINTS += [f'/v{i % 3}/item/{i}/{{id}}' for i in range(1829)]
# Same length/front/middle/back 8-byte CRC samples, different full keys.
COLLISION_ENDPOINTS = ('/api/v2/' + 'a' * 32, '/api/v2/' + 'a' * 5 + 'b' + 'a' * 26)
ENDPOINTS[13:15] = COLLISION_ENDPOINTS
STATUSES = (200, 201, 202, 204, 301, 302, 304, 400, 401, 403, 404,
            405, 409, 410, 422, 429, 499, 500, 501, 502, 503, 504, 599)


def check(binary, seed, count):
    rng = random.Random(seed)
    hours = [(BASE + dt.timedelta(hours=h)).strftime('%Y-%m-%dT%H:') for h in range(72)]
    rows = []
    for i in range(count):
        hour = rng.randrange(72)
        endpoint = ENDPOINTS[rng.randrange(13 if i % 5 == 0 else len(ENDPOINTS))]
        latency = rng.randrange(100000) if i % 13 else (0 if i % 2 == 0 else 4294967295)
        if i % 31 == 0: latency = (1 << 24) - 1
        elif i % 37 == 0: latency = 1 << 24
        status = rng.choice(STATUSES)
        ts = hours[hour] + f'{rng.randrange(60):02d}:{rng.randrange(60):02d}.{rng.randrange(1000000):06d}Z'
        rows.append(f'{ts} {METHODS[i % 7]} {endpoint} {status} {latency}\n')
    if count >= 2:
        rows[0] = f'{hours[0]}00:00.000000Z GET {COLLISION_ENDPOINTS[0]} 200 1\n'
        rows[1] = f'{hours[0]}00:00.000000Z GET {COLLISION_ENDPOINTS[1]} 500 2\n'
    if count > 4:
        rows[2] = rows[1]
    grouped = defaultdict(lambda: [[], 0])
    for line in rows:
        ts, _, endpoint, status, latency = line.rstrip('\n').split(' ')
        values, errors = grouped[(ts[:13], endpoint)]
        values.append(int(latency))
        grouped[(ts[:13], endpoint)][1] += 500 <= int(status) <= 599
    expected = []
    for (hour, endpoint), (values, errors) in sorted(grouped.items()):
        values.sort()
        n = len(values)
        p = lambda q: values[(q * n + 99) // 100 - 1]
        expected.append(f'{hour}:00:00Z {endpoint} {n} {p(50)} {p(95)} {p(99)} {errors} {errors * 1000000 // n}\n')
    with tempfile.TemporaryDirectory() as directory:
        data, out = Path(directory) / 'data', Path(directory) / 'out'
        data.mkdir()
        out.mkdir()
        (data / 'requests.log').write_text(''.join(rows))
        env = dict(os.environ, SD_DATA_DIR=str(data), SD_OUT_DIR=str(out))
        subprocess.run([binary], env=env, check=True)
        actual = (out / 'summary.txt').read_text()
        assert actual == ''.join(expected), f'differential failure seed={seed}, count={count}'
    print(f'PASS synthetic seed={seed} rows={count} groups={len(grouped)}', flush=True)


if __name__ == '__main__':
    for n in (0, 1, 2, 3, 4, 1000, 10000, 100000, 310000):
        check(sys.argv[1], 1, n)
    for seed in (2, 3, 42):
        check(sys.argv[1], seed, 100000)
