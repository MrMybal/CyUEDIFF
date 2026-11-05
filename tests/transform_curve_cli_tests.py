"""Public transform curve families, raw components and sampled CLI comparisons."""
from pathlib import Path
import tempfile,subprocess,json,sys,hashlib
cli,generator=(str(Path(p).resolve()) for p in sys.argv[1:3]);checks=0
with tempfile.TemporaryDirectory(prefix='transform-curve-cli-') as tmp:
 root=Path(tmp);subprocess.run([generator,'--write-fixtures',str(root)],check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.uasset')}
 def run(*args,code=0):
  global checks
  r=subprocess.run([cli,*map(str,args)],capture_output=True,timeout=30)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'))
  checks+=1
  return json.loads(r.stdout) if r.stdout else None
 family=['--curve-family','transform'];lane='name:Root~/control/translation.x'
 for version in (0,1004,1013):
  files=[root/f'transform-{n}-{version}.uasset' for n in range(17)];a,b=files[:2]
  snapshot=run('curves',a,*family);assert snapshot['complete'] and len(snapshot['owners']['TestAsset']['lanes'])==18
  assert not run('curves',a)['owners']['TestAsset']['lanes']
  assert run('curves-diff',a,a,*family)['status']=='identical'
  assert len(run('curves-diff',a,b,*family,code=1)['changes'])==1
  assert run('curves-diff',a,files[2],*family)['status']=='identical'
  for n in (3,4,5,7,8,16):assert run('curves-diff',files[n],files[n],*family,code=2)['status']=='incomplete'
  assert run('curve-value',a,'--time','1','--lane',lane,*family)['value']==1.
  value=run('curve-components',a,'--time','1','--curve-id','name:Root~/control',*family)
  assert value['ready'] and value['values']['rotation.y']==2. and len(value['values'])==9
  assert not run('curve-components',files[4],'--time','1','--curve-id','name:Root~/control',*family,code=2)['ready']
  assert run('curve-components',files[11],'--time','100','--curve-id','name:Hand',*family)['ready']
  assert run('curve-components',files[15],'--time','-1','--curve-id','name:Hand',*family)['ready']
  for n in (10,12,13,14):assert not run('curve-value',files[n],'--time','1','--lane',lane,*family,code=2)['ready']
  grid=['--time-start','0','--time-end','2','--samples','5','--before-lane',lane,'--after-lane',lane]
  d=run('curve-value-diff',a,b,*grid,*family,code=1);assert d['changedSamples']==4 and d['maximumAbsoluteDelta']==2.
  assert run('curve-value-diff',a,b,*grid,*family,'--value-tolerance','2')['status']=='identical'
  for args in [('curve-components',a,*family),('curve-components',a,'--time','1',*family),('curve-components',a,'--time','nan',*family),('curves',a,'--curve-family','unknown'),('inspect',a,*family),('curves',a,'--curve-id','name:Hand'),('curve-components',a,'--time','1','--lane',lane,*family),('curve-components',a,'--time','1','--curve-id','name:Hand',*family,'--metadata'),('curve-components',a,'--time','1','--curve-id','name:Hand',*family,'--object','missing')]:run(*args,code=2)
  for target in (a,a.with_suffix('.uexp')):run('curve-components',a,'--time','1','--curve-id','name:Hand',*family,'--out',target,code=2)
  out=root/'components.json';run('curve-components',a,'--time','1','--curve-id','name:Hand',*family,'--out',out);assert json.loads(out.read_text())['ready']
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
help_text=subprocess.check_output([cli,'--help'],text=True)
assert any(line.startswith('  cyuediff curve-components ') for line in help_text.splitlines())
print(f'{checks} transform curve CLI checks passed')
