"""Validate the animation tab and workspace state through the native host."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,generator=(str(Path(x).resolve()) for x in sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='cyuediff-animation-desktop-') as tmp:
 root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
 subprocess.run([generator,'--write-fixtures',str(root)],check=True,env=env)
 before=root/'animation-0-1013.uasset';after=root/'animation-1-1013.uasset'
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in [before,after]}
 state={'asset':{'animations':{'beforeObject':'TestAsset','afterObject':'TestAsset','frame':1,'component':0,
        'inspection':{'selected':'name:arm~/track','changedOnly':True,'filter':'arm','properties':{'filter':'positions','changedOnly':False}}}}}
 report=root/'report.json';workspace=root/'workspace.json'
 def run(*args):
  global checks
  r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
  assert r.returncode==0,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(report.read_text(encoding='utf-8'))
 first=run('--before',before,'--after',after,'--view','animations','--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace)
 assert first['status']=='ready'
 saved=json.loads(workspace.read_text(encoding='utf-8'))
 assert saved['documents'][0]['view']=='animations'
 actual=saved['documents'][0]['viewState']['asset']['animations']
 assert actual['frame']==1 and actual['inspection']['selected']=='name:arm~/track'
 original=workspace.read_bytes()
 reopened=run('--workspace',workspace)
 assert reopened['workspaceFile']['loaded'] and workspace.read_bytes()==original
 assert reopened['workspaceFile']['context']['documents'][0]['viewState']['asset']['animations']==actual
 for locale in ('en','fr'):
  run('--workspace',workspace,'--locale',locale)
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
 assert not (root/'unused-profile').exists()
print(f'{checks} animation desktop process/workspace checks passed')
