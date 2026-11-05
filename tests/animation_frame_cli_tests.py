"""Exercise independent local animation frame comparisons and protected outputs."""
from pathlib import Path
import subprocess,tempfile,json,hashlib,sys
cli,generator=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
with tempfile.TemporaryDirectory(prefix='animation-frame-diff-cli-') as tmp:
 root=Path(tmp);subprocess.run([generator,'--write-fixtures',str(root)],check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.uasset')}
 def run(*args,code=0):
  global checks
  r=subprocess.run([cli,*map(str,args)],capture_output=True,timeout=30);assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(r.stdout) if r.stdout else None
 for version in (1004,1013):
  files=[root/f'animation-{n}-{version}.uasset' for n in range(18)];a,b=files[:2];frame=['--before-frame','1','--after-frame','1']
  assert run('animation-frame-diff',a,a,*frame)['status']=='identical'
  d=run('animation-frame-diff',a,b,*frame,code=1);assert d['changedTracks']==1 and d['tracks']['name:arm~/track']['metrics']['positionDistance']==1.
  assert run('animation-frame-diff',a,b,*frame,'--position-tolerance','1')['status']=='identical'
  assert run('animation-frame-diff',a,a,'--before-frame','0','--after-frame','2',code=1)['changedTracks']==2
  assert run('animation-frame-diff',a,files[2],*frame)['status']=='identical'
  for n in (3,5,10,12,13):assert run('animation-frame-diff',a,files[n],*frame,code=2)['status']=='incomplete'
  assert run('animation-frame-diff',a,files[4],*frame)['status']=='identical'
  assert run('animation-frame-diff',a,files[6],*frame)['status']=='identical'
  assert run('animation-diff',a,files[6],code=1)['status']=='changed'
  assert run('animation-frame-diff',a,files[7],*frame,code=1)['tracks']['name:arm~/track']['metrics']['rotationDeltaDegrees']>89.9
  assert run('animation-frame-diff',a,files[7],*frame,'--rotation-tolerance','90')['status']=='identical'
  assert run('animation-frame-diff',a,files[8],*frame,'--scale-tolerance','2')['status']=='identical'
  assert run('animation-frame-diff',a,files[9],*frame,code=1)['changedTracks']==2
  assert run('animation-frame-diff',a,files[11],*frame,code=1)['tracks']['name:arm~/track']['status']=='removed'
  assert run('animation-frame-diff',a,files[14],*frame)['status']=='identical'
  assert run('animation-frame-diff',a,files[15],*frame,'--position-tolerance','.25')['status']=='identical'
  assert run('animation-frame-diff',a,files[16],*frame,'--rotation-tolerance','180')['status']=='identical'
  for args in [('animation-frame-diff',a,b),('animation-frame-diff',a,b,'--before-frame','1'),('animation-frame-diff',a,b,'--before-frame','-1','--after-frame','0'),('animation-frame-diff',a,b,'--before-frame','1.5','--after-frame','0'),('animation-frame-diff',a,b,'--before-frame','3','--after-frame','0'),('animation-frame-diff',a,b,*frame,'--rotation-tolerance','181'),('animation-frame-diff',a,b,*frame,'--scale-tolerance','-1'),('animation-frame-diff',a,b,*frame,'--position-tolerance','nan'),('animation-frame-diff',a,b,*frame,'--metadata'),('animation-frame-diff',a,b,*frame,'--snapshot'),('animation-frame-diff',a,b,*frame,'--frame','1'),('animation-frame-diff',a,b,*frame,'--before-object','missing'),('animation',a,'--rotation-tolerance','1'),('animation',a,'--before-frame','1')]:run(*args,code=2)
  for output in (a,a.with_suffix('.uexp')):run('animation-frame-diff',a,b,*frame,'--out',output,code=2)
  output=root/'comparison.json';run('animation-frame-diff',a,b,*frame,'--out',output,code=1);assert json.loads(output.read_text())['changedTracks']==1
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
print(f'{checks} animation frame CLI checks passed')
