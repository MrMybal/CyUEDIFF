"""Stored animation metadata contracts through the public CLI."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,generator=(str(Path(p).resolve()) for p in sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='animation-cli-') as directory:
 root=Path(directory);subprocess.run([generator,'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 def run(*args,code=0):
  global checks
  p=subprocess.run([cli,*map(str,args)],capture_output=True,env=env,timeout=30)
  assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(p.stdout or p.stderr) if p.stdout or p.stderr else None
 before,after,reorder,stripped,bad=(root/(name+'.uasset') for name in ('animation-before','animation-after','animation-reordered','animation-stripped','animation-invalid'))
 snapshot=run('skeleton-animation',before)
 assert snapshot['schema']=='cyuediff.skeleton-animation/1' and snapshot['complete']
 assert snapshot['skeletons']['TestAsset']['syncMarkers']['names']['Left']['occurrences']==2
 assert run('skeleton-animation-diff',before,after,code=1)['status']=='changed'
 assert run('skeleton-animation-diff',before,reorder)['status']=='identical'
 for file in (stripped,bad):
  assert not run('skeleton-animation',file)['complete']
  assert run('skeleton-animation-diff',file,file,code=2)['status']=='incomplete'
 for command,args in (('skeleton-animation',[before]),('skeleton-animation-diff',[before,after])):
  run(command,*args,'--metadata',code=2)
  for target in (before,before.with_suffix('.uexp')):
   run(command,*args,'--out',target,code=2)
  run(command,*args,'--retarget-source','A',code=2)
 out=root/'animation.json';run('skeleton-animation',before,'--out',out)
 assert json.loads(out.read_text(encoding='utf-8'))==snapshot
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());checks+=len(hashes)
print(f'{checks} animation CLI checks passed')
