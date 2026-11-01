"""Three immutable complete-program controls; fresh-output matched T/M receipts."""
import argparse, hashlib, json, os, random, statistics, subprocess, time
from pathlib import Path
p=argparse.ArgumentParser()
p.add_argument('--work',required=True);p.add_argument('--organizer',required=True)
for n in ('baseline','official','candidate'):p.add_argument('--'+n,required=True)
p.add_argument('--trials',type=int,default=21);p.add_argument('--memory-trials',type=int,default=11)
a=p.parse_args();work=Path(a.work);work.mkdir(parents=True,exist_ok=True)
out=work/'out';out.mkdir(exist_ok=True);out.chmod(0o777)
rng=random.Random(20261102);timing=[];names=['baseline','official','candidate']
for seed in (1,2,3):
 data=work/f'data{seed}'
 subprocess.run(['python',a.organizer+'/generators/a_ingest.py','--seed',str(seed),'--scale','full','--out',str(data)],check=True)
 (data/'requests.log').read_bytes();expected=None
 for threads in (1,2,6,12):
  samples={n:[] for n in names}
  for trial in range(a.trials+2):
   order=names.copy();rng.shuffle(order)
   for n in order:
    (out/'summary.txt').unlink(missing_ok=True)
    env=dict(os.environ,SD_THREADS=str(threads),SD_DATA_DIR=str(data),SD_OUT_DIR=str(out))
    time.sleep(.105);start=time.perf_counter_ns()
    subprocess.run([getattr(a,n)],env=env,check=True)
    elapsed=(time.perf_counter_ns()-start)/1e9
    digest=hashlib.sha256((out/'summary.txt').read_bytes()).hexdigest();expected=expected or digest
    assert digest==expected,(seed,threads,n)
    if seed==1:assert digest=='495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3'
    if trial>=2:samples[n].append(elapsed)
  row=dict(seed=seed,threads=threads,samples=samples,medians={n:statistics.median(s) for n,s in samples.items()},sha256=expected)
  timing.append(row);print('TIMING '+json.dumps({k:v for k,v in row.items() if k!='samples'}),flush=True)
  (work/'timing.json').write_text(json.dumps(timing))
 memory=[]
 for threads in (6,12):
  for trial in range(a.memory_trials):
   order=names.copy();rng.shuffle(order)
   for n in order:
    (out/'summary.txt').unlink(missing_ok=True)
    image={'baseline':'sd-baseline','official':'sd-official','candidate':'sd-track-a'}[n]
    cmd=['docker','run','--rm','--network=none','--read-only','--memory=8g','--memory-swap=8g','--pids-limit=512','--tmpfs','/tmp:rw,size=1g','-e',f'SD_THREADS={threads}','-v',f'{data}:/data:ro','-v',f'{out}:/out','--entrypoint','/bin/sh',image,'-c','/solution && cat /sys/fs/cgroup/memory.peak && cat /sys/fs/cgroup/memory.stat']
    lines=subprocess.check_output(cmd,text=True).splitlines()
    row=dict(seed=seed,threads=threads,trial=trial,name=n,peak=int(lines[0]),stat={k:int(v) for k,v in (x.split() for x in lines[1:])})
    memory.append(row)
    assert hashlib.sha256((out/'summary.txt').read_bytes()).hexdigest()==expected
 (work/f'memory-seed{seed}.json').write_text(json.dumps(memory))
 print('MEMORY '+json.dumps([{k:v for k,v in x.items() if k!='stat'} for x in memory]),flush=True)
print('TIMING_SAMPLES '+json.dumps(timing),flush=True)
