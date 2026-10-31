"""Reordered official-sample inputs must give the reference output byte for byte.

The solution finalizes hours early when the file is nearly time-ordered and
falls back to an exact rerun otherwise; reversed and shuffled files take the
fallback, a time-sorted file takes the early path.
"""
import os
import random
import subprocess
import sys
import tempfile

binary, organizer = sys.argv[1], sys.argv[2]


def run(argv, data):
    out = tempfile.mkdtemp()
    env = dict(os.environ, SD_DATA_DIR=data, SD_OUT_DIR=out)
    subprocess.run(argv + (['--data', data, '--out', out] if len(argv) > 1 else []), env=env, check=True, capture_output=True)
    with open(out + '/summary.txt', 'rb') as f:
        return f.read()


for seed in (4, 5):
    data = tempfile.mkdtemp()
    subprocess.run([sys.executable, organizer + '/generators/a_ingest.py', '--seed', str(seed), '--scale', 'sample', '--out', data],
                   check=True, capture_output=True)
    with open(data + '/requests.log', 'rb') as f:
        lines = f.read().split(b'\n')[:-1]
    variants = {'official': lines, 'reversed': lines[::-1], 'sorted': sorted(lines),
                'shuffled': random.Random(seed).sample(lines, len(lines))}
    for name, rows in variants.items():
        directory = tempfile.mkdtemp()
        with open(directory + '/requests.log', 'wb') as f:
            f.write(b'\n'.join(rows) + b'\n')
        expected = run([sys.executable, organizer + '/reference/a_ingest.py'], directory)
        assert run([binary], directory) == expected, f'seed={seed} order={name}'
        print(f'PASS seed={seed} order={name}', flush=True)
