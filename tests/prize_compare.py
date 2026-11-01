"""Matched full-program timing and fresh-output container memory receipts."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import random
import statistics
import subprocess
import time

p = argparse.ArgumentParser()
p.add_argument('--work', required=True)
p.add_argument('--organizer', required=True)
p.add_argument('--champion', required=True)
p.add_argument('--candidate', required=True)
p.add_argument('--docker', action='store_true')
p.add_argument('--trials', type=int, default=31)
a = p.parse_args()
work = Path(a.work); work.mkdir(parents=True, exist_ok=True)
out = work / 'out'; out.mkdir(exist_ok=True); out.chmod(0o777)
rng = random.Random(20261101)
rows = []
for seed in (1, 2, 3):
    data = work / f'data{seed}'
    subprocess.run(['python', a.organizer + '/generators/a_ingest.py', '--seed', str(seed), '--scale', 'full', '--out', str(data)], check=True)
    Path(data / 'requests.log').read_bytes()
    expected = None
    for threads in (1, 2, 12):
        samples = {'champion': [], 'candidate': []}
        for trial in range(a.trials + 2):
            order = list(samples); rng.shuffle(order)
            for name in order:
                (out / 'summary.txt').unlink(missing_ok=True)
                env = dict(os.environ, SD_THREADS=str(threads), SD_DATA_DIR=str(data), SD_OUT_DIR=str(out))
                started = time.perf_counter_ns()
                subprocess.run([getattr(a, name)], env=env, check=True)
                elapsed = (time.perf_counter_ns() - started) / 1e9
                digest = hashlib.sha256((out / 'summary.txt').read_bytes()).hexdigest()
                expected = expected or digest
                assert digest == expected, (seed, threads, name, digest, expected)
                if seed == 1:
                    assert digest == '495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3'
                if trial >= 2:
                    samples[name].append(elapsed)
        row = dict(seed=seed, threads=threads, samples=samples, medians={k: statistics.median(v) for k, v in samples.items()}, sha256=expected)
        rows.append(row)
        print(json.dumps({k:v for k,v in row.items() if k!='samples'}), flush=True)
        (work / 'timing.json').write_text(json.dumps(rows, indent=2))
    if a.docker:
        memory = []
        for threads in (2, 12):
            for trial in range(15):
                order = ['champion', 'candidate']; rng.shuffle(order)
                for name in order:
                    (out / 'summary.txt').unlink(missing_ok=True)
                    image = 'sd-champion' if name == 'champion' else 'sd-track-a'
                    cmd = ['docker', 'run', '--rm', '--network=none', '--read-only', '--memory=8g', '--memory-swap=8g', '--pids-limit=512', '--tmpfs', '/tmp:rw,size=1g', '-e', f'SD_THREADS={threads}', '-v', f'{data}:/data:ro', '-v', f'{out}:/out', '--entrypoint', '/bin/sh', image, '-c', '/solution && cat /sys/fs/cgroup/memory.peak && cat /sys/fs/cgroup/memory.stat']
                    lines = subprocess.check_output(cmd, text=True).splitlines()
                    row = dict(seed=seed, threads=threads, trial=trial, name=name, peak=int(lines[0]), stat={k:int(v) for k,v in (line.split() for line in lines[1:])})
                    memory.append(row)
                    assert hashlib.sha256((out / 'summary.txt').read_bytes()).hexdigest() == expected
        (work / f'memory-seed{seed}.json').write_text(json.dumps(memory, indent=2))
