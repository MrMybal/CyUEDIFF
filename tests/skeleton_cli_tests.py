"""Read independent native-prefix packages through the public command-line tools."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,generator,mesh_generator=map(lambda x:Path(x).resolve(),sys.argv[1:4])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='skeleton-cli-') as directory:
 root=Path(directory)
 subprocess.run([str(generator),'--write-fixtures',str(root)],env=env,check=True)
 subprocess.run([str(mesh_generator),'--write-fixtures',str(root)],env=env,check=True)
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
 displacement=run('skeleton-pose-diff',before,after,code=1)
 assert displacement['changedJoints']==1 and displacement['joints']['arm']['delta']==[1.,0.,0.]
 assert run('skeleton-pose-diff',before,reordered)['status']=='identical'
 assert run('skeleton-pose-diff',before,invalid,code=2)['status']=='incomplete'
 assert run('skeleton-pose-diff',before,after,'--position-tolerance','1')['status']=='identical'
 assert run('skeleton-pose-diff',before,after,'--position-tolerance','0.99',code=1)['status']=='changed'
 for bad in ('-1','nan','inf','1oops','1,5',''):
  run('skeleton-pose-diff',before,after,'--position-tolerance',bad,code=2)
 run('skeleton-pose-diff',before,after,'--before-object','missing',code=2)
 run('skeleton-pose-diff',before,after,'--after-object','missing',code=2)
 for option in ('--metadata','--snapshot'):
  run('skeleton-pose-diff',before,after,option,code=2)
 for target in (before,after,before.with_suffix('.uexp')):
  run('skeleton-pose-diff',before,after,'--out',target,code=2)
 for option in ('--position-tolerance','--before-object','--after-object'):
  run('skeleton',before,option,'1',code=2)
 output=root/'displacement.json'
 run('skeleton-pose-diff',before,after,'--out',output,code=1)
 assert json.loads(output.read_text(encoding='utf-8'))==displacement
 mesh_before,mesh_after,mesh_reorder,mesh_invalid=(root/(name+'.uasset') for name in ('mesh-before','mesh-after','mesh-reordered','mesh-invalid'))
 assert run('skeleton',mesh_before)['skeletons']['TestAsset']['mesh']['materials'][0]['slotName']=='Body'
 assert run('skeleton-diff',mesh_before,mesh_reorder)['status']=='identical'
 assert run('skeleton-pose',mesh_before)['joints']['arm']['position']==[10.,0.,0.]
 mesh_diff=run('skeleton-pose-diff',mesh_before,mesh_after,'--before-object','TestAsset','--after-object','TestAsset',code=1)
 assert mesh_diff['joints']['arm']['distance']==2.
 assert run('skeleton-pose-diff',mesh_before,mesh_invalid,code=2)['status']=='incomplete'
 for p,digest in hashes.items():
  assert hashlib.sha256(p.read_bytes()).hexdigest()==digest
  checks+=1
print(f'{checks} skeleton CLI checks passed')
