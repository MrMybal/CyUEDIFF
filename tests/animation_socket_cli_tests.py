"""Binary source socket origins, independent contexts, malformed inputs and protected outputs."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile,shutil
cli,generator=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-animation-socket-cli-',dir=Path.cwd()) as tmp:
 root=Path(tmp);subprocess.run([generator,'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.uasset')}
 def run(*args,code=0):
  global checks
  r=subprocess.run([cli,*map(str,args)],env=env,capture_output=True,timeout=30)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(r.stdout) if r.stdout else None
 for version in (1004,1013):
  a=root/f'socket-animation-0-{version}.uasset';b=root/f'socket-animation-1-{version}.uasset';s=root/f'socket-rig-0-{version}.uasset'
  expected=run('animation-socket-pose',a,s,'--frame','1');assert expected['complete'] and expected['sockets']['Rig.Grip']['position']==[21.,22.,23.]
  assert run('animation-socket-pose',a,s,'--frame','1','--skeleton-object','Rig')==expected
  base=['--before-skeleton',s,'--after-skeleton',s,'--before-frame','1','--after-frame','1']
  assert run('animation-socket-pose-diff',a,a,*base)['status']=='identical'
  changed=run('animation-socket-pose-diff',a,b,*base,code=1);assert changed['changedSockets']==2 and changed['sockets']['Rig.Grip']['distance']==1.
  assert run('animation-socket-pose-diff',a,b,*base,'--position-tolerance','1')['status']=='identical'
  for variant in (2,3,6,9):
   unknown=run('animation-socket-pose',a,root/f'socket-rig-{variant}-{version}.uasset','--frame','1',code=2);assert not unknown['complete']
  for variant in (4,7,8):assert run('animation-socket-pose',a,root/f'socket-rig-{variant}-{version}.uasset','--frame','1')['complete']
  missing=root/f'socket-rig-3-{version}.uasset'
  d=run('animation-socket-pose-diff',a,a,'--before-skeleton',s,'--after-skeleton',missing,'--before-frame','1','--after-frame','1',code=2);assert d['sockets']['Rig.Grip']['kind']=='unavailable'
  d=run('animation-socket-pose-diff',a,a,'--before-skeleton',s,'--after-skeleton',root/f'socket-rig-5-{version}.uasset','--before-frame','1','--after-frame','1','--position-tolerance','1000',code=1);assert d['sockets']['Rig.Grip']['parentChanged']
  for args in [('animation-socket-pose',a,s),('animation-socket-pose',a,s,'--frame','3'),('animation-socket-pose',a,s,'--frame','-1'),('animation-socket-pose',a,s,'--frame','0','--metadata'),('animation-socket-pose',a,s,'--frame','0','--object','missing'),('animation-socket-pose',a,s,'--frame','0','--skeleton-object','missing'),('animation-socket-pose-diff',a,a),('animation-socket-pose-diff',a,b,*base,'--rotation-tolerance','1'),('animation-socket-pose-diff',a,b,*base,'--position-tolerance','nan'),('animation-socket-pose-diff',a,b,*base,'--position-tolerance','-1'),('animation',a,'--skeleton-object','Rig')]:run(*args,code=2)
  for file in (a,s,s.with_suffix('.uexp'),root/f'socket-rig-1-{version}.uasset'):
   run('animation-socket-pose-diff',a,a,'--before-skeleton',s,'--after-skeleton',root/f'socket-rig-1-{version}.uasset','--before-frame','1','--after-frame','1','--out',file,code=2)
  for side in ('before','after'):
   content=root/side/'Content';content.mkdir(parents=True,exist_ok=True);(root/side/'Game.uproject').write_text('{"FileVersion":3}',encoding='utf-8')
   shutil.copyfile(a if side=='before' else b,content/'Clip.uasset');shutil.copyfile(s,content/'Rig.uasset')
  d=run('animation-socket-pose-diff','/Game/Clip','/Game/Clip','--before-skeleton','/Game/Rig','--after-skeleton','/Game/Rig','--before-project',root/'before/Game.uproject','--after-project',root/'after/Game.uproject','--before-frame','1','--after-frame','1',code=1);assert d['changedSockets']==2
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
print(f'{checks} source animation socket CLI checks passed')
