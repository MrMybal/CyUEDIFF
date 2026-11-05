"""Explicit stored membership, virtual definitions and socket-origin CLI contracts."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,generator=(str(Path(p).resolve()) for p in sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='attachments-cli-') as directory:
 root=Path(directory);subprocess.run([generator,'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 def run(*args,code=0):
  global checks
  p=subprocess.run([cli,*map(str,args)],capture_output=True,env=env,timeout=30)
  assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(p.stdout or p.stderr) if p.stdout or p.stderr else None
 for version in (1004,1013):
  before,after,reorder,missing,cycle,empty=(root/f'attachments-{name}-{version}.uasset' for name in ('before','after','reordered','missing','cycle','empty'))
  snapshot=run('attachments',before)
  assert snapshot['complete'] and snapshot['owners']['Rig']['virtualBones']['definitions']['name:VB_tip']['sourceResolution']=='virtual'
  assert run('attachments-diff',before,after,code=1)['status']=='changed'
  assert run('attachments-diff',before,reorder)['status']=='identical'
  assert run('attachments-diff',cycle,cycle,code=2)['status']=='incomplete'
  pose=run('socket-pose',before,'--object','Rig')
  assert pose['complete'] and pose['sockets']['Rig.Grip']['position']==[11.,2.,3.]
  assert run('socket-pose',before,'--retarget-source','A~/pose')['sockets']['Rig.Grip']['position']==[16.,2.,3.]
  diff=run('socket-pose-diff',before,after,code=1)
  assert diff['changedSockets']==1 and diff['sockets']['Rig.Grip']['distance']==3.
  assert run('socket-pose-diff',before,after,'--position-tolerance','3')['status']=='identical'
  assert run('socket-pose-diff',before,before,'--before-retarget-source','A~/pose','--after-retarget-source','',code=1)['sockets']['Rig.Grip']['delta']==[-5.,0.,0.]
  assert not run('socket-pose',missing)['complete']
  assert run('socket-pose-diff',missing,missing,code=2)['status']=='incomplete'
  assert run('socket-pose',cycle)['complete']
  assert run('socket-pose',empty)['sockets']=={}
  run('socket-pose',before,'--object','Missing',code=2)
  run('socket-pose',before,'--retarget-source','Missing',code=2)
  for command,args in (('attachments',[before]),('socket-pose',[before]),('attachments-diff',[before,after]),('socket-pose-diff',[before,after])):
   run(command,*args,'--metadata',code=2)
   for target in (before,before.with_suffix('.uexp')):
    run(command,*args,'--out',target,code=2)
  run('attachments',before,'--retarget-source','A',code=2)
  run('socket-pose-diff',before,after,'--position-tolerance','nan',code=2)
  out=root/f'pose-{version}.json';run('socket-pose',before,'--out',out)
  assert json.loads(out.read_text(encoding='utf-8'))==pose
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in hashes.items());checks+=len(hashes)
print(f'{checks} attachment CLI checks passed')
