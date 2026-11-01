#!/usr/bin/env python3
"""Interleaved, fresh-container cgroup memory.peak comparison."""
import random
import statistics
import subprocess

cases={"main":"sd-current-main-axes","certificate":"sd-track-a"}
measure={k:[] for k in cases}
for i in range(25):
    order=list(cases)
    random.Random(20261101+i).shuffle(order)
    pair={}
    for k in order:
        cmd=[
            "docker","run","--rm","--network=none","--read-only",
            "--memory=8g","--memory-swap=8g","--pids-limit=512",
            "--tmpfs","/tmp:rw,size=1g",
            "--mount","type=bind,source=/tmp/a-full,target=/data,readonly",
            "--mount","type=bind,source=/tmp/a-container-out,target=/out",
            "--entrypoint","/bin/sh",cases[k],
            "-c","/solution && cat /sys/fs/cgroup/memory.peak",
        ]
        peak=int(subprocess.check_output(cmd,text=True).strip())
        measure[k].append(peak)
        pair[k]=peak
    print(f"PAIR_AXES_M i={i+1} main={pair['main']} cert={pair['certificate']} delta={pair['certificate']-pair['main']}",flush=True)
for k,v in measure.items():
    print(f"SUMMARY_AXES_M {k} min={min(v)} median={statistics.median(v)} max={max(v)} mean={statistics.mean(v):.0f}",flush=True)
delta=[b-a for a,b in zip(measure["main"],measure["certificate"])]
print(f"SUMMARY_AXES_M paired_delta_median={statistics.median(delta)} certificate_higher={sum(d>0 for d in delta)}/25",flush=True)
for b in range(5):
    a=measure["main"][b*5:(b+1)*5]
    c=measure["certificate"][b*5:(b+1)*5]
    print(f"FIVE_AXES_M batch={b+1} main_max={max(a)} cert_max={max(c)} ratio={max(c)/max(a):.4f}",flush=True)
