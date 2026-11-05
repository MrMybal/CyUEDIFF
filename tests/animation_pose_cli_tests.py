"""Explicit Skeleton bindings, independent contexts and protected animation pose outputs."""
from pathlib import Path
import subprocess,tempfile,json,hashlib,sys,shutil
cli,generator=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
with tempfile.TemporaryDirectory(prefix='animation-pose-cli-') as tmp:
 root=Path(tmp);subprocess.run([generator,'--write-fixtures',str(root)],check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*') if p.is_file()}
 def run(*args,code=0):
  global checks
  r=subprocess.run([cli,*map(str,args)],capture_output=True,timeout=30)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(r.stdout) if r.stdout else None
 s=root/'skeleton-0.uasset'
 for v in (1004,1013):
  a=root/f'animation-0-{v}.uasset';b=root/f'animation-1-{v}.uasset'
  p=run('animation-pose',a,s,'--frame','1');assert p['complete'] and p['joints']['arm~/track']['position']==[20.,20.,20.]
  args=['--before-skeleton',s,'--after-skeleton',s,'--before-frame','1','--after-frame','1']
  assert run('animation-pose-diff',a,a,*args)['status']=='identical'
  d=run('animation-pose-diff',a,b,*args,code=1);assert d['changedJoints']==1 and d['joints']['arm~/track']['distance']==1.
  assert run('animation-pose-diff',a,b,*args,'--position-tolerance','1')['status']=='identical'
  assert run('animation-pose',a,root/'skeleton-4.uasset','--frame','1')['joints']==p['joints']
  temporal=run('animation-pose-diff',a,a,'--before-skeleton',s,'--after-skeleton',s,'--before-frame','0','--after-frame','2',code=1);assert temporal['changedJoints']==2
  assert run('animation-pose',a,root/'skeleton-1.uasset','--frame','1',code=2)['unboundTracks']==['name:arm~/track']
  assert run('animation-pose',a,root/'skeleton-3.uasset','--frame','1',code=2)['joints']['tip']['position'] is None
  reparent=run('animation-pose-diff',a,a,'--before-skeleton',s,'--after-skeleton',root/'skeleton-2.uasset','--before-frame','0','--after-frame','0','--position-tolerance','1000',code=1);assert reparent['joints']['arm~/track']['parentChanged']
  for n in (3,5,9,10,11,12,13):assert not run('animation-pose',root/f'animation-{n}-{v}.uasset',s,'--frame','1',code=2)['complete']
  for args_bad in [('animation-pose',a,s),('animation-pose',a,s,'--frame','-1'),('animation-pose',a,s,'--frame','3'),('animation-pose',a,s,'--frame','0','--metadata'),('animation-pose',a,s,'--frame','0','--snapshot'),('animation-pose',a,s,'--frame','0','--object','missing'),('animation-pose',a,s,'--frame','0','--skeleton-object','missing'),('animation-pose-diff',a,b),('animation-pose-diff',a,b,*args,'--rotation-tolerance','10'),('animation-pose-diff',a,b,*args,'--position-tolerance','nan'),('animation-pose-diff',a,b,*args,'--position-tolerance','-1'),('animation-pose-diff',a,b,*args,'--before-skeleton-object','missing'),('animation',a,'--before-skeleton',s),('animation-frame',a,'--frame','0','--skeleton-object','TestAsset')]:run(*args_bad,code=2)
  for target in (a,a.with_suffix('.uexp'),s,s.with_suffix('.uexp')):run('animation-pose',a,s,'--frame','0','--out',target,code=2)
  output=root/'pose.json';run('animation-pose',a,s,'--frame','1','--out',output);assert json.loads(output.read_text())['complete']
  # Both Skeleton inputs participate in output protection, including companion paths.
  t=root/'skeleton-4.uasset'
  for target in (s,t,t.with_suffix('.uexp')):run('animation-pose-diff',a,a,'--before-skeleton',s,'--after-skeleton',t,'--before-frame','0','--after-frame','0','--out',target,code=2)
  for side in ('before','after'):
   project=root/side;content=project/'Content';content.mkdir(parents=True,exist_ok=True)
   (project/'Test.uproject').write_text('{"FileVersion":3}',encoding='utf-8')
   for source,target in ((a if side=='before' else b,'Clip'),(s,'Rig')):
    for ext in ('.uasset','.uexp'):
     if source.with_suffix(ext).exists():shutil.copyfile(source.with_suffix(ext),content/(target+ext))
  d=run('animation-pose-diff','/Game/Clip','/Game/Clip','--before-skeleton','/Game/Rig','--after-skeleton','/Game/Rig','--before-project',root/'before/Test.uproject','--after-project',root/'after/Test.uproject','--before-frame','1','--after-frame','1',code=1);assert d['changedJoints']==1
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
print(f'{checks} animation hierarchy CLI checks passed')
