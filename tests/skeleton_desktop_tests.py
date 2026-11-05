"""Skeleton tab selection and camera/property state survive explicit workspace restart."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,generator=(str(Path(p).resolve()) for p in sys.argv[1:3])
env={k.upper():v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-skeleton-desktop-',dir=Path.cwd()) as directory:
 root=Path(directory);env['LOCALAPPDATA']=str(root/'unused-profile')
 subprocess.run([generator,'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 report=root/'report.json'
 def run(*args,code=0):
  global checks
  report.unlink(missing_ok=True)
  r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'))
  checks+=1
  return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
 state={'asset':{'skeleton':{'object':'TestAsset','canvas':{'pan':[-20.,-30.],'zoom':1.1},
         'inspection':{'selected':'arm','properties':{'filter':'translation','selected':'/localPose/translation/0'}}}}}
 workspace=root/'workspace.json'
 saved=run('--before',root/'before.uasset','--after',root/'after.uasset','--view','skeleton',
           '--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace)
 document=saved['workspace']['documents'][0]
 assert json.loads(workspace.read_text(encoding='utf-8'))['documents'][0]['view']=='skeleton' and document['viewState']['asset']['skeleton']['inspection']['selected']=='arm'
 restored=run('--workspace',workspace,'--locale','fr')
 assert restored['workspace']['documents'][0]['viewState']['asset']['skeleton']==document['viewState']['asset']['skeleton']
 pose_state={'asset':{'skeleton':{'object':'TestAsset','posePreview':True,
             'poseCamera':{'target':[10.,0.,0.],'up':[0.,0.,1.],'yaw':0.7,'pitch':0.35,'distance':35.},
             'inspection':{'selected':'arm'}}}}
 pose_workspace=root/'pose-workspace.json'
 pose_saved=run('--before',root/'before.uasset','--after',root/'after.uasset','--view','skeleton',
                '--document-action','view-state:'+json.dumps(pose_state),'--save-workspace',pose_workspace)
 pose_document=pose_saved['workspace']['documents'][0]['viewState']['asset']['skeleton']
 pose_restored=run('--workspace',pose_workspace,'--locale','fr')
 assert pose_document['posePreview'] and pose_restored['workspace']['documents'][0]['viewState']['asset']['skeleton']==pose_document
 for name in ('invalid','unsupported'):
  assert run('--before',root/(name+'.uasset'),'--view','skeleton')['status']=='ready'
 run('--before',root/'before.uasset','--view','skeleton','--save-workspace',root/'before.uasset',code=2)
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items())
 assert not (root/'unused-profile').exists()
 checks+=4
print(f'{checks} skeleton desktop checks passed')
