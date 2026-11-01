"""Official topology: persistent host tmpfs, fresh run parent, external data prewarm."""
import argparse,json,os,random,shutil,statistics,subprocess,sys,time,uuid
from pathlib import Path
p=argparse.ArgumentParser()
for key in ('work','organizer'):p.add_argument('--'+key,required=True)
p.add_argument('--trials',type=int,default=15)
p.add_argument('--probe-image');p.add_argument('--data')
p.add_argument('--seeds',type=int,nargs='+',default=[1,2,3]);p.add_argument('--threads',type=int,nargs='+',default=[1,2,6,12])
a=p.parse_args();root=Path(a.work);root.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,a.organizer+'/harness')
from run_submission import prewarm,spread_ok
names=['baseline','official','candidate'];images={'baseline':'sd-baseline','official':'sd-official','candidate':'sd-track-a'}
rng=random.Random(20261104);timing=[];memory=[]
if a.probe_image:
 names=['candidate'];images['candidate']=a.probe_image
if a.data:assert len(a.seeds)==1
timer=root/'timer';timer.mkdir(exist_ok=True);timer.chmod(0o755)
subprocess.run(['cc','-O2',a.organizer+'/harness/timer/sd-timer.c','-o',str(timer/'sd-timer')],check=True)
(timer/'sd-timer').chmod(0o755)
volume='sd-r2-work-'+uuid.uuid4().hex[:12]
helper='sd-r2-reader-'+uuid.uuid4().hex[:12]
subprocess.run(['docker','volume','create','--driver','local','--opt','type=tmpfs','--opt','device=tmpfs','--opt','o=size=8g,mode=0755',volume],check=True,stdout=subprocess.DEVNULL)
mountpoint=subprocess.check_output(['docker','volume','inspect','-f','{{.Mountpoint}}',volume],text=True).strip()
subprocess.run(['docker','run','-d','--name',helper,'--network=none','--read-only','--user=0:0','--cgroupns=host','--mount',f'type=volume,source={volume},target=/srv/speeddemon/work','--mount','type=bind,source=/sys/fs/cgroup,target=/host-cgroup','--entrypoint','/bin/sleep','sd-track-a','infinity'],check=True,stdout=subprocess.DEVNULL)
def helper_cmd(command):
 return subprocess.check_output(['docker','exec',helper,'/bin/sh','-ec',command],text=True)
fs=helper_cmd('stat -f -c "%T %s %b" /srv/speeddemon/work').split()
assert fs[0]=='tmpfs' and int(fs[1])*int(fs[2])==8*1024**3,fs
cpus=','.join(map(str,sorted(os.sched_getaffinity(0))))
(root/'environment.json').write_text(json.dumps(dict(mode='persistent-tmpfs-parent-cgroup',filesystem=fs[0],tmpfs_bytes=8*1024**3,cpuset=cpus,timer='organizer/harness/timer/sd-timer.c',parent='/speeddemon/<fresh-run>',prewarm='run_submission.prewarm before creating run cgroup')))
def run_once(name,data,threads):
 run='run-'+uuid.uuid4().hex[:12];directory='/srv/speeddemon/work/'+run;cg='/host-cgroup/speeddemon/'+run
 helper_cmd(f'mkdir -p {directory}/out {directory}/sd\nchmod 777 {directory}/out\nchmod 700 {directory}/sd')
 prewarm(str(data)) # outside the fresh parent cgroup, exactly as the harness
 cid='sd-'+run
 command=['docker','run','--name',cid,'--no-healthcheck','--user=0:0',f'--cgroup-parent=/speeddemon/{run}',f'--cpuset-cpus={cpus}','--cpuset-mems=0','--memory=8g','--memory-swap=8g','--pids-limit=512','--network=none','--read-only','--tmpfs','/tmp:rw,size=1g,exec','--cap-drop=ALL','--cap-add=SETUID','--cap-add=SETGID','--security-opt=no-new-privileges','--ulimit','nofile=8192:8192','-v',f'{data}:/data:ro','-v',f'{mountpoint}/{run}/out:/out:rw','-v',f'{mountpoint}/{run}/sd:/sd:rw','-v',f'{timer}:/sd-bin:ro','-e',f'SD_THREADS={threads}','--entrypoint','/sd-bin/sd-timer',images[name],'/sd/timing.json','65534','65534','--','/solution']
 try:
  proc=subprocess.run(command,check=False,capture_output=True,text=True,timeout=120)
  if proc.returncode:raise RuntimeError((name,proc.returncode,proc.stderr[-2000:]))
 finally:subprocess.run(['docker','rm','-fv',cid],check=False,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 # The subject container and its leaf cgroup are gone before the parent's peak
 # is read. The output file remains on the persistent tmpfs during measurement.
 lines=helper_cmd(f'cat {directory}/sd/timing.json\ncat {cg}/memory.peak\ncat {cg}/memory.current\ncat {cg}/memory.stat\nrmdir {cg}\nsha256sum {directory}/out/summary.txt\nstat -c %s {directory}/out/summary.txt\nstat -f -c %T {directory}/out\nrm -rf {directory}').splitlines()
 clock=json.loads(lines[0]);assert clock['exit_code']==0 and clock['signal']==0
 peak=int(lines[1]);size=int(lines[-2]);assert lines[-1]=='tmpfs' and peak>=size
 stat={k:int(v) for k,v in (line.split() for line in lines[3:-3])}
 return dict(T=clock['wall_ns']/1e9,peak=peak,end_current=int(lines[2]),stat=stat,sha256=lines[-3].split()[0],output_bytes=size,run_id=run,diagnostic_stdout=proc.stdout)
try:
 for seed in a.seeds:
  data=Path(a.data).resolve() if a.data else (root/f'data{seed}').resolve()
  if not a.data:subprocess.run(['python',a.organizer+'/generators/a_ingest.py','--seed',str(seed),'--scale','full','--out',str(data)],check=True)
  data.chmod(0o755)
  for file in data.iterdir():file.chmod(0o644)
  expected=None
  for threads in a.threads:
   samples={n:[] for n in names};batch=[]
   for trial in range(-2,a.trials):
    order=names.copy();rng.shuffle(order)
    for name in order:
     time.sleep(.105)
     row=run_once(name,data,threads);expected=expected or row['sha256'];assert row['sha256']==expected,(seed,threads,name)
     if seed==1 and not a.probe_image:assert expected=='495ca09682b9673ef836a506496177680bb4a3af54255174befddb1f83481cd3'
     row.update(seed=seed,threads=threads,name=name,trial=trial,warmup=trial<0,mode='persistent-tmpfs-parent-cgroup')
     memory.append(row);batch.append({k:v for k,v in row.items() if k!='stat'} | {'end_shmem':row['stat']['shmem'],'end_file':row['stat']['file']})
     if trial>=0:samples[name].append(row['T'])
     (root/'fresh-runs.json').write_text(json.dumps(memory))
   row=dict(seed=seed,threads=threads,samples=samples,medians={n:statistics.median(v) for n,v in samples.items()},sha256=expected,output_bytes=batch[-1]['output_bytes'],mode='persistent-tmpfs-parent-cgroup')
   timing.append(row);(root/'timing.json').write_text(json.dumps(timing))
   print(('RECLAMATION_SUMMARY ' if a.probe_image else 'TIMING ')+json.dumps({k:v for k,v in row.items() if k!='samples'}),flush=True)
   print(('RECLAMATION ' if a.probe_image else 'MEMORY ')+json.dumps(batch),flush=True)
 print(('RECLAMATION_TIMES ' if a.probe_image else 'TIMING_SAMPLES ')+json.dumps(timing),flush=True)
finally:
 subprocess.run(['docker','rm','-f',helper],check=False,stdout=subprocess.DEVNULL)
 subprocess.run(['docker','volume','rm',volume],check=False,stdout=subprocess.DEVNULL)
