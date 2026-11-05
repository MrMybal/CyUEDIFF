"""Native transform curve family and component workspace persistence."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,generator=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='transform-desktop-') as tmp:
 root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile');subprocess.run([generator,'--write-fixtures',str(root)],check=True,env=env)
 a=root/'transform-0-1013.uasset';b=root/'transform-1-1013.uasset';hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in (a,b)}
 state={'asset':{'curves':{'animationFamily':'transform','beforeObject':'TestAsset','afterObject':'TestAsset','lane':'name:Root~/control/translation.x','time':1.,'customRange':True,'rangeBegin':-1.,'rangeEnd':3.,'valueTolerance':.1,'inspection':{'selected':'time:4000000000000000','properties':{'filter':'value','changedOnly':False}}}}}
 report=root/'report.json';workspace=root/'workspace.json'
 def run(*args):
  global checks
  r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
  assert r.returncode==0,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(report.read_text(encoding='utf-8'))
 assert run('--before',a,'--after',b,'--view','curves','--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace)['status']=='ready'
 saved=json.loads(workspace.read_text());actual=saved['documents'][0]['viewState']['asset']['curves']
 assert actual['animationFamily']=='transform' and actual['lane']=='name:Root~/control/translation.x' and actual['time']==1. and actual['valueTolerance']==.1
 original=workspace.read_bytes()
 reopened=run('--workspace',workspace);assert reopened['workspaceFile']['context']['documents'][0]['viewState']['asset']['curves']==actual and workspace.read_bytes()==original
 for locale in ('en','fr'):assert run('--workspace',workspace,'--locale',locale)['status']=='ready'
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
 assert not (root/'unused-profile').exists()
print(f'{checks} transform curve desktop/workspace process checks passed')
