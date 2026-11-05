"""Exercise the installed-facing animation commands with independently encoded packages."""
import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
cli,fixtures=map(lambda x:str(Path(x).resolve()),sys.argv[1:3])
checks=0
with tempfile.TemporaryDirectory(prefix='cyuediff-animation-cli-') as temp:
 root=Path(temp)
 subprocess.run([fixtures,'--write-fixtures',str(root)],check=True)
 def run(*args,code=0):
  global checks
  r=subprocess.run([cli,*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=30)
  assert r.returncode==code,(args,r.returncode,r.stderr)
  checks+=1
  return json.loads(r.stdout) if r.stdout.strip() else None
 for version in (1004,1013):
  files=[root/f'animation-{n}-{version}.uasset' for n in range(6)]
  before,after,order,unavailable,missing_rate,duplicate=files
  hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
  snap=run('animation',before)
  assert snap['complete'] and len(snap['models']['TestAsset']['tracks'])==2
  assert run('animation-diff',before,before)['status']=='identical'
  assert run('animation-diff',before,order)['status']=='identical'
  diff=run('animation-diff',before,after,code=1)
  assert diff['changes'][0]['path']=='/tracks/name:arm~0~1track/positions/1/0'
  assert run('animation-diff',missing_rate,missing_rate,code=2)['status']=='incomplete'
  assert run('animation-diff',duplicate,duplicate,code=2)['status']=='incomplete'
  assert run('animation-frame',before,'--frame','2')['tracks']['name:root']['positions']==[20,20,20]
  assert not run('animation-frame',unavailable,'--frame','1')['complete']
  assert 'timeSeconds' not in run('animation-frame',missing_rate,'--frame','1')
  for args in [('animation-frame',before),('animation-frame',before,'--frame','-1'),
               ('animation-frame',before,'--frame','3'),('animation-frame',before,'--frame','1.5'),
               ('animation-frame',before,'--frame','4294967296'),('animation',before,'--metadata'),
               ('animation',before,'--snapshot'),('animation',before,'--frame','1'),
               ('animation-diff',before,after,'--position-tolerance','1'),
               ('animation-diff',before,after,'--before-object','missing'),
               ('animation-frame',before,'--frame','0','--object','missing')]:
   run(*args,code=2)
  destination=root/'report.json'
  run('animation-frame',before,'--frame','0','--out',destination)
  assert json.loads(destination.read_text(encoding='utf-8'))['frame']==0
  for destination in [before,root/(before.stem+'.uexp')]:
   run('animation',before,'--out',destination,code=2)
  assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
print(f'{checks} animation source CLI checks passed')
