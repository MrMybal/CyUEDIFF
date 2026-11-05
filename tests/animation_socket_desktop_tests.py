"""Native source socket preparation and persisted review from explicit loose Skeletons."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,generator=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-animation-socket-desktop-',dir=Path.cwd()) as tmp:
 root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile');subprocess.run([generator,'--write-fixtures',str(root)],env=env,check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.uasset')};report=root/'report.json';workspace=root/'workspace.json'
 def run(*args):
  global checks
  r=subprocess.run([exe,'--smoke','--no-preferences','--no-previews','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=60)
  assert r.returncode==0,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1;return json.loads(report.read_text(encoding='utf-8'))
 def sockets(data):return data['document']['animationPose']['socketOrigins']
 a=root/'socket-animation-0-1013.uasset';b=root/'socket-animation-1-1013.uasset';s=root/'socket-rig-0-1013.uasset'
 base=['--before',a,'--after',b,'--pose-before-skeleton',s,'--pose-after-skeleton',s,'--pose-frame','1','--view','animations']
 state={'asset':{'animationPosePreview':True,'animationSocketsPreview':True,'animationSockets':{'selected':'Rig.Grip','filter':'Grip','positionTolerance':.5,'camera':{'target':[20,20,20],'distance':80,'yaw':.3,'pitch':.4}}}}
 d=run(*base,'--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace);assert sockets(d)['complete'] and sockets(d)['comparison']['changedSockets']==2
 saved=json.loads(workspace.read_text(encoding='utf-8'));expected=saved['documents'][0]['viewState']['asset']['animationSockets'];original=workspace.read_bytes()
 d=run('--workspace',workspace);assert sockets(d)['complete'] and d['workspace']['documents'][0]['viewState']['asset']['animationSockets']==expected and workspace.read_bytes()==original
 for actions in [('reload',),('duplicate',),('close','reopen')]:
  args=['--workspace',workspace]
  for action in actions:args+=['--document-action',action]
  d=run(*args);assert sockets(d)['complete'] and d['workspace']['documents'][-1]['viewState']['asset']['animationSockets']==expected
 d=run(*base,'--pose-after-skeleton',root/'socket-rig-3-1013.uasset');assert sockets(d)['comparison']['sockets']['Rig.Grip']['kind']=='unavailable'
 d=run(*base,'--pose-after-skeleton',root/'socket-rig-4-1013.uasset');assert sockets(d)['comparison']['sockets']['Rig.Grip']['kind']=='removed'
 d=run('--before',a,'--pose-before-skeleton',s,'--pose-frame','1');assert sockets(d)['status']=='ready' and sockets(d)['after'] is None
 d=run(*base,'--pose-after-skeleton',root/'missing.uasset');assert not sockets(d)['complete'] and sockets(d)['before']['complete']
 d=run(*base,'--document-action','view-state:'+json.dumps({'asset':{'animationSocketsPreview':False}}));assert not d['workspace']['documents'][0]['viewState']['asset']['animationSocketsPreview']
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());assert not (root/'unused-profile').exists()
print(f'{checks} native source socket/workspace checks passed')
