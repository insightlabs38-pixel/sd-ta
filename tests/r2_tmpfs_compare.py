"""Matched production binaries, official sd-timer, and tmpfs output as on the board."""
import argparse,hashlib,json,os,random,shutil,statistics,subprocess,time
from pathlib import Path
p=argparse.ArgumentParser()
for key in ('work','organizer','baseline','official','candidate'):p.add_argument('--'+key,required=True)
p.add_argument('--trials',type=int,default=21);p.add_argument('--memory-trials',type=int,default=11)
a=p.parse_args();root=Path(a.work);root.mkdir(parents=True,exist_ok=True)
bins=root/'binaries';bins.mkdir(exist_ok=True);timer=root/'timer';timer.mkdir(exist_ok=True)
for name in ('baseline','official','candidate'):shutil.copyfile(getattr(a,name),bins/name);(bins/name).chmod(0o755)
subprocess.run(['cc','-O2',a.organizer+'/harness/timer/sd-timer.c','-o',str(timer/'sd-timer')],check=True)
names=['baseline','official','candidate'];rng=random.Random(20261103);timing=[]
def docker_args(data):
 return ['--network=none','--read-only','--user=0:0','--memory=8g','--memory-swap=8g','--pids-limit=512','--cap-drop=ALL','--cap-add=SETUID','--cap-add=SETGID','--security-opt=no-new-privileges','--tmpfs','/tmp:rw,size=1g','--tmpfs','/out:rw,size=1g,mode=1777','--tmpfs','/sd:rw,size=1m,mode=0700','-v',f'{data}:/data:ro','-v',f'{bins}:/binaries:ro','-v',f'{timer}:/sd-bin:ro']
def result(lines):
 value=json.loads(lines[0]);assert value['exit_code']==0 and value['signal']==0
 return value['wall_ns']/1e9,lines[1].split()[0]
for seed in (1,2,3):
 data=root/f'data{seed}'
 subprocess.run(['python',a.organizer+'/generators/a_ingest.py','--seed',str(seed),'--scale','full','--out',str(data)],check=True)
 (data/'requests.log').read_bytes();expected=None
 for threads in (1,2,6,12):
  cid=subprocess.check_output(['docker','run','-d',*docker_args(data),'-e',f'SD_THREADS={threads}','--entrypoint','/bin/sleep','sd-track-a','infinity'],text=True).strip()
  samples={n:[] for n in names}
  try:
   for trial in range(a.trials+2):
    order=names.copy();rng.shuffle(order)
    for name in order:
     time.sleep(.105)
     command=f'rm -f /out/summary.txt /sd/timing.json; /sd-bin/sd-timer /sd/timing.json 65534 65534 -- /binaries/{name} && cat /sd/timing.json && sha256sum /out/summary.txt'
     elapsed,digest=result(subprocess.check_output(['docker','exec',cid,'/bin/sh','-c',command],text=True).splitlines())
     expected=expected or digest;assert digest==expected,(seed,threads,name)
     if seed==1:assert digest=='495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3'
     if trial>=2:samples[name].append(elapsed)
   row=dict(seed=seed,threads=threads,samples=samples,medians={n:statistics.median(x) for n,x in samples.items()},sha256=expected,mode='tmpfs-sd-timer')
   timing.append(row);print('TIMING '+json.dumps({k:v for k,v in row.items() if k!='samples'}),flush=True)
   (root/'timing.json').write_text(json.dumps(timing))
  finally:subprocess.run(['docker','rm','-f',cid],check=True,stdout=subprocess.DEVNULL)
 memory=[]
 for threads in (6,12):
  for trial in range(a.memory_trials):
   order=names.copy();rng.shuffle(order)
   for name in order:
    command=f'/sd-bin/sd-timer /sd/timing.json 65534 65534 -- /binaries/{name} && cat /sys/fs/cgroup/memory.peak && cat /sys/fs/cgroup/memory.stat && sha256sum /out/summary.txt'
    lines=subprocess.check_output(['docker','run','--rm',*docker_args(data),'-e',f'SD_THREADS={threads}','--entrypoint','/bin/sh','sd-track-a','-c',command],text=True).splitlines()
    assert lines[-1].split()[0]==expected
    row=dict(seed=seed,threads=threads,trial=trial,name=name,peak=int(lines[0]),stat={k:int(v) for k,v in (x.split() for x in lines[1:-1])},mode='tmpfs-sd-timer')
    memory.append(row)
 (root/f'memory-seed{seed}.json').write_text(json.dumps(memory))
 print('MEMORY '+json.dumps([{k:v for k,v in r.items() if k!='stat'} | {'end_shmem':r['stat']['shmem'],'end_file':r['stat']['file']} for r in memory]),flush=True)
print('TIMING_SAMPLES '+json.dumps(timing),flush=True)
