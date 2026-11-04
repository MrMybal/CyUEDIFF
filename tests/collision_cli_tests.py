"""Exercise collision commands through independently encoded tagged package files."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,generator=map(lambda x:Path(x).resolve(),sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='collision-cli-') as folder:
 root=Path(folder)
 subprocess.run([str(generator),'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 def run(*args,code=0):
  global checks
  r=subprocess.run([str(cli),*map(str,args)],capture_output=True,env=env,timeout=25)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'))
  checks+=1
  return json.loads(r.stdout or r.stderr) if r.stdout or r.stderr else None
 for version in ('53','55'):
  a,b,c,d=(root/(name+version+'.uasset') for name in ('before','after','reordered','incomplete'))
  model=run('collision',a)
  assert model['complete'] and len(model['bodies']['TestAsset']['shapes'])==6
  assert model['bodies']['TestAsset']['shapes']['sphere:name:Ball']['geometry']['radius']==5
  checks+=2
  wire=run('collision-preview',a)
  assert wire['schema']=='cyuediff.collision-wireframe/1' and wire['space']=='body-local'
  assert wire['sourceComplete'] and len(wire['shapes'])==6 and all(s['ready'] for s in wire['shapes'])
  hull=next(s for s in wire['shapes'] if s['kind']=='convex')
  assert len(hull['lines'])==6
  checks+=3
  partial=run('collision-preview',d)
  assert not partial['sourceComplete'] and sum(not s['ready'] for s in partial['shapes'])==1
  checks+=1
  run('collision-preview',a,'--metadata',code=2)
  run('collision-preview',a,'--out',a,code=2)
  run('collision-preview',a,'--out',a.with_suffix('.uexp'),code=2)
  preview_target=root/('wireframe-'+version+'.json')
  run('collision-preview',a,'--out',preview_target)
  assert json.loads(preview_target.read_text(encoding='utf-8'))==wire
  checks+=1
  changed=run('collision-diff',a,b,code=1)
  assert len(changed['changes'])==1 and changed['changes'][0]['path'].endswith('/geometry/radius')
  checks+=1
  assert run('collision-diff',a,c)['status']=='identical'
  assert run('collision-diff',a,a)['complete']
  assert run('collision-diff',a,d,code=2)['status']=='incomplete'
  checks+=3
  assert run('inspect',a)['collisions']==model
  assert run('diff',a,b,code=1)['collisionChanges']
  checks+=2
  run('collision',a,'--metadata',code=2)
  run('collision-diff',a,b,'--metadata',code=2)
  run('collision',a,'--out',a,code=2)
  run('collision-diff',a,b,'--out',b,code=2)
  run('collision',a,'--out',a.with_suffix('.uexp'),code=2)
  for command in ('diff','material-diff'):
   run(command,a,b,'--out',b,code=2)
  if hasattr(os,'link'):
   alias=root/('alias-'+version+'.uasset')
   os.link(a,alias)
   run('collision',a,'--out',alias,code=2)
  target=root/('report-'+version+'.json')
  run('collision',a,'--out',target)
  assert json.loads(target.read_text(encoding='utf-8'))==model
  checks+=1
 assert not run('collision',root/'no-collision.uasset')['complete']
 assert run('collision-diff',root/'no-collision.uasset',root/'no-collision.uasset',code=2)['status']=='incomplete'
 run('collision-diff',root/'before53.uasset',code=2)
 for path,digest in hashes.items():
  assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
  checks+=1
print(f'{checks} collision CLI checks passed')
