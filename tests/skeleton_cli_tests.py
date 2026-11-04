"""Read independent native-prefix packages through the public command-line tools."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,generator=map(lambda x:Path(x).resolve(),sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='skeleton-cli-') as directory:
 root=Path(directory)
 subprocess.run([str(generator),'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 def run(*args,code=0):
  global checks
  r=subprocess.run([str(cli),*map(str,args)],env=env,capture_output=True,timeout=30)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'))
  checks+=1
  return json.loads(r.stdout or r.stderr) if r.stdout or r.stderr else None
 before,after,reordered,invalid,unsupported=(root/(name+'.uasset') for name in ('before','after','reordered','invalid','unsupported'))
 snapshot=run('skeleton',before)
 assert snapshot['complete'] and snapshot['skeletons']['TestAsset']['bones']['arm']['parent']=='root'
 assert run('skeleton-diff',before,after,code=1)['changes'][0]['path'].endswith('/arm/localPose/translation/0')
 assert run('skeleton-diff',before,reordered)['status']=='identical'
 assert run('skeleton-diff',before,invalid,code=2)['status']=='incomplete'
 assert run('inspect',before)['skeletons']==snapshot
 assert run('diff',before,after,code=1)['skeletonChanges']
 pose=run('skeleton-pose',before,'--object','TestAsset')
 assert pose['space']=='raw-reference-affine' and pose['joints']['arm']['position']==[10.,0.,0.]
 assert not run('skeleton-pose',invalid)['complete']
 run('skeleton-pose',before,'--object','missing',code=2)
 assert not run('skeleton',unsupported)['complete']
 for command in ('skeleton','skeleton-pose'):
  run(command,before,'--metadata',code=2)
  run(command,before,'--out',before,code=2)
  run(command,before,'--out',before.with_suffix('.uexp'),code=2)
  if hasattr(os,'link'):
   alias=root/(command+'-alias.uasset');os.link(before,alias)
   run(command,before,'--out',alias,code=2)
  output=root/(command+'.json')
  run(command,before,'--out',output)
  assert json.loads(output.read_text(encoding='utf-8'))==(snapshot if command=='skeleton' else pose)
 run('skeleton-diff',before,after,'--metadata',code=2)
 run('skeleton-diff',before,after,'--out',after,code=2)
 run('skeleton-diff',before,code=2)
 for p,digest in hashes.items():
  assert hashlib.sha256(p.read_bytes()).hexdigest()==digest
  checks+=1
print(f'{checks} skeleton CLI checks passed')
