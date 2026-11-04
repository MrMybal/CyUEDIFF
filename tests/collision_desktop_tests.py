"""Collision selection, typed previews and explicit workspace restart."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,fixture=(str(Path(p).resolve()) for p in sys.argv[1:3])
env={k.upper():v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-collision-desktop-',dir=Path.cwd()) as folder:
 root=Path(folder)
 env['LOCALAPPDATA']=str(root/'unused-profile')
 subprocess.run([fixture,'--write-fixtures',str(root)],env=env,check=True)
 fingerprints={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 report=root/'report.json'
 def run(*args,code=0):
  global checks
  report.unlink(missing_ok=True)
  r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
  assert r.returncode==code,(r.returncode,r.stderr.decode('utf-8','replace'),args)
  checks+=1
  return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
 for version in ('53','55'):
  workspace=root/('workspace-'+version+'.json')
  state={'asset':{'collisions':{'body':'TestAsset','shape':'sphere:name:Ball',
                               'properties':{'filter':'radius','selected':'/geometry/radius','changedOnly':True}}}}
  saved=run('--before',root/('before'+version+'.uasset'),'--after',root/('after'+version+'.uasset'),
            '--view','collisions','--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace)
  document=saved['workspace']['documents'][0]
  assert json.loads(workspace.read_text(encoding='utf-8'))['documents'][0]['view']=='collisions'
  assert document['viewState']['asset']['collisions']['shape']=='sphere:name:Ball'
  assert document['viewState']['asset']['collisions']['properties']['selected']=='/geometry/radius'
  checks+=2
  restored=run('--workspace',workspace,'--locale','fr')
  assert restored['document']==saved['document'] and restored['workspace']['documents'][0]['viewState']['asset']['collisions']==document['viewState']['asset']['collisions']
  checks+=1
 for name in ('incomplete55','no-collision'):
  assert run('--before',root/(name+'.uasset'),'--view','collisions')['status']=='ready'
  checks+=1
 for target in ('before53.uasset','after53.uasset'):
  run('--before',root/'before53.uasset','--after',root/'after53.uasset','--view','collisions','--save-workspace',root/target,code=2)
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in fingerprints.items())
 assert not (root/'unused-profile').exists()
 checks+=2
print(f'{checks} collision desktop checks passed')
