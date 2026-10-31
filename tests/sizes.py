"""Groups of every size 1..80 and a few larger, with ties and uint32 extremes, against a sorting oracle."""
import os
import random
import subprocess
import sys
import tempfile

rng = random.Random(11)
EXTREME = (0, 1, 2 ** 31 - 1, 2 ** 31, 2 ** 31 + 1, 4294967294, 4294967295)
sizes = list(range(1, 81)) + [99, 100, 101, 127, 128, 129, 255, 256, 257, 1000, 4097]
rows, groups = [], {}
for i, n in enumerate(sizes):
    for kind in range(4):
        hour, name = (i * 4 + kind) % 72, f'/v2/size{i}k{kind}'
        if kind == 0:
            values = [rng.randrange(1000) for _ in range(n)]
        elif kind == 1:
            values = [rng.choice(EXTREME) for _ in range(n)]
        elif kind == 2:
            values = [rng.choice((5, 5, 5, 9)) for _ in range(n)]
        else:
            values = sorted(rng.randrange(10 ** 9) for _ in range(n))
            if i % 2:
                values.reverse()
        groups[(hour, name)] = values
        for j, v in enumerate(values):
            h = 18 + hour
            day = ['2026-10-30', '2026-10-31', '2026-11-01', '2026-11-02'][h // 24]
            rows.append(f'{day}T{h % 24:02d}:{j % 60:02d}:{j % 50:02d}.000001Z GET {name} {500 if j % 5 == 0 else 200} {v}\n')
rng.shuffle(rows)  # forces the exact fallback; sorted input exercises the streaming path below
want = []
for (hour, name), values in sorted(groups.items(), key=lambda kv: (kv[0][0], kv[0][1].encode())):
    h = 18 + hour
    day = ['2026-10-30', '2026-10-31', '2026-11-01', '2026-11-02'][h // 24]
    s, n = sorted(values), len(values)
    pick = lambda q: s[(q * n + 99) // 100 - 1]
    errors = sum(1 for j in range(n) if j % 5 == 0)
    want.append(f'{day}T{h % 24:02d}:00:00Z {name} {n} {pick(50)} {pick(95)} {pick(99)} {errors} {errors * 1000000 // n}\n')
for order, ordered in (('shuffled', rows), ('sorted', sorted(rows))):
    with tempfile.TemporaryDirectory() as d:
        os.mkdir(d + '/data')
        os.mkdir(d + '/out')
        open(d + '/data/requests.log', 'w').write(''.join(ordered))
        subprocess.run([sys.argv[1]], env=dict(os.environ, SD_DATA_DIR=d + '/data', SD_OUT_DIR=d + '/out'), check=True)
        got = open(d + '/out/summary.txt').read()
        assert got == ''.join(want), order
    print('PASS sizes', order, len(want), 'groups', flush=True)
