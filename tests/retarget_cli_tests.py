"""Exercise native retarget arrays through installed-style public CLI contracts."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,generator=(str(Path(p).resolve()) for p in sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='retarget-cli-') as directory:
 root=Path(directory);subprocess.run([generator,'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 def run(*args,code=0):
  global checks
  p=subprocess.run([cli,*map(str,args)],capture_output=True,env=env,timeout=30)
  assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(p.stdout or p.stderr) if p.stdout or p.stderr else None
 before,after,reorder,bad,mismatch=(root/(name+'.uasset') for name in ('retarget-before','retarget-after','retarget-reordered','retarget-invalid','retarget-mismatch'))
 source='A~/pose'
 pose=run('skeleton-pose',before,'--retarget-source',source)
 assert pose['complete'] and pose['retargetSource']==source and pose['joints']['arm']['position']==[15.,0.,0.]
 assert run('skeleton-pose',before)['joints']['arm']['position']==[10.,0.,0.]
 diff=run('skeleton-pose-diff',before,after,'--before-retarget-source',source,code=1)
 assert diff['beforeRetargetSource']==source and diff['afterRetargetSource']==source and diff['joints']['arm']['distance']==2.
 assert run('skeleton-pose-diff',before,after,'--before-retarget-source',source,'--after-retarget-source','B')['status']=='identical'
 raw=run('skeleton-pose-diff',before,before,'--before-retarget-source',source,'--after-retarget-source','',code=1)
 assert raw['afterRetargetSource'] is None and raw['joints']['arm']['delta']==[-5.,0.,0.]
 assert run('skeleton-diff',before,reorder)['status']=='identical'
 assert run('skeleton-pose-diff',before,after,'--before-retarget-source',source,'--position-tolerance','2')['status']=='identical'
 for file in (bad,mismatch):
  assert not run('skeleton-pose',file,'--retarget-source',source)['complete']
  assert run('skeleton-pose-diff',before,file,'--before-retarget-source',source,code=2)['status']=='incomplete'
 run('skeleton-pose',before,'--retarget-source','DisplayPose',code=2)
 run('skeleton-pose-diff',before,after,'--after-retarget-source','Missing',code=2)
 for command,flag in (('skeleton','--retarget-source'),('skeleton','--before-retarget-source'),('skeleton-pose','--after-retarget-source')):
  run(command,before,flag,source,code=2)
 for target in (before,before.with_suffix('.uexp')):
  run('skeleton-pose',before,'--retarget-source',source,'--out',target,code=2)
 alias=root/'alias.uasset'
 if hasattr(os,'link'):
  os.link(before,alias);run('skeleton-pose',before,'--retarget-source',source,'--out',alias,code=2)
 output=root/'pose.json';run('skeleton-pose',before,'--retarget-source',source,'--out',output)
 assert json.loads(output.read_text(encoding='utf-8'))==pose
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items());checks+=len(hashes)
print(f'{checks} retarget CLI checks passed')
