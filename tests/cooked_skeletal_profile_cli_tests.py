"""Named native weight profiles reach CLI differences and the bilingual desktop inspector."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,base_fixture,profiles_fixture=[str(Path(p).resolve()) for p in sys.argv[1:4]]
desktop=str(Path(sys.argv[4]).resolve()) if len(sys.argv)>4 else None
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-skeletal-profiles-cli-',dir=Path.cwd()) as temp:
 root=Path(temp)
 for fixture in (base_fixture,profiles_fixture):subprocess.run([fixture,'--write-fixtures',str(root)],check=True,capture_output=True,env=env,timeout=30)
 context=json.loads((root/'context.json').read_text(encoding='utf-8'));context['usmap']='after.usmap';context['bindings']['/Script/Engine.Unused']={'schema':'Collections','offset':0}
 (root/'after.usmap').write_bytes((root/'native.usmap').read_bytes());(root/'after-context.json').write_text(json.dumps(context),encoding='utf-8')
 protected={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
 entry='000000000000000000000001'
 common=['--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'context.json','--skeletal-profile','ue5.3-windows','--keep-mobile-min-lod','0','--has-vertex-colors','0']
 def run(command,*args,code=0):
  global checks
  result=subprocess.run([cli,command,*map(str,common),*map(str,args)],env=env,capture_output=True,timeout=30)
  assert result.returncode==code,(result.returncode,result.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(result.stdout) if code!=2 else None
 before=root/'profiles.utoc';after=root/'profiles-changed.utoc'
 decoded=run('zen-skeletal',before,entry);lod=decoded['lods'][0]
 assert lod['weightProfiles'][0]['name']=='Texture_0' and lod['geometry']['skin']['vertices']['0']['profile:Texture_0']=={'Texture':49344,'PF_DXT1':16191};checks+=1
 assert run('zen-skeletal-diff',before,entry,before,entry)['status']=='identical';checks+=1
 changed=run('zen-skeletal-diff',before,entry,after,entry,'--after-schema',root/'after-context.json',code=1)
 assert changed['status']=='changed' and changed['beforeMesh']['schemaContextSha256']!=changed['afterMesh']['schemaContextSha256'];checks+=1
 streamed=run('zen-skeletal',root/'profiles-streamed.utoc',entry)
 assert all(l['weightProfiles'][0]['name']=='Texture_0' for l in streamed['lods']);checks+=1
 if desktop:
  before_flags=['--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'context.json','--skeletal-profile','ue5.3-windows','--skeletal-keep-mobile-min-lod','0','--has-vertex-colors','0']
  after_flags=['--after-cooked-profile','ue5.3','--after-script-objects',root/'global.utoc','--after-schema',root/'after-context.json','--after-skeletal-profile','ue5.3-windows','--after-skeletal-keep-mobile-min-lod','0','--after-has-vertex-colors','0']
  state={'container':{'mesh':'/Game/Texture.Texture LOD 0','meshSkin':True,'meshSkinView':{'selected':'vertex/0','properties':{'changedOnly':True,'filter':'profile:Texture_0'}}}}
  for locale in ('en','fr'):
   report=root/(locale+'.json');workspace=root/(locale+'-workspace.json')
   result=subprocess.run([desktop,'--smoke','--no-preferences','--no-restore-session','--locale',locale,'--container',str(before),'--container-entry',entry,'--after-container',str(root/'profiles-renamed-changed.utoc'),'--after-container-entry',entry,'--container-tab','meshes','--report',str(report),'--save-workspace',str(workspace),*map(str,before_flags),*map(str,after_flags),'--document-action','view-state:'+json.dumps(state,separators=(',',':'))],capture_output=True,env=env,timeout=45)
   assert result.returncode==0,result.stderr.decode('utf-8','replace')
   data=json.loads(report.read_text(encoding='utf-8'));views=data['containerView']['meshViews']
   assert data['locale']==locale and all(v['comparisonKnown'] and v['skin']['modified']==2 for v in views)
   pair=data['document']['container']['meshDifferences'][0];assert pair['pairing']=='explicit-members-single-mesh-lod' and pair['beforeKey']!=pair['afterKey']
   assert all(v['skin']['selected']=='vertex/0' for v in views if not v['after'])
   saved=json.loads(workspace.read_text(encoding='utf-8'));assert saved['documents'][0]['viewState']['container']['meshSkin']
   checks+=4
  report=root/'implicit.json'
  result=subprocess.run([desktop,'--smoke','--no-preferences','--container',str(before),'--container-entry',entry,'--after-container',str(root/'profiles-renamed-changed.utoc'),'--container-tab','meshes','--report',str(report),*map(str,before_flags),*map(str,after_flags)],capture_output=True,env=env,timeout=45)
  assert result.returncode==0,result.stderr.decode('utf-8','replace')
  views=json.loads(report.read_text(encoding='utf-8'))['containerView']['meshViews']
  assert all(not v['comparisonKnown'] and not v['skin']['comparing'] and v['skin']['modified']==0 for v in views)
  checks+=2
 for name in ('profiles.utoc','profiles.ucas','profiles-changed.utoc','profiles-changed.ucas','context.json','after-context.json','native.usmap','after.usmap','global.utoc','global.ucas'):
  run('zen-skeletal-diff',before,entry,after,entry,'--after-schema',root/'after-context.json','--out',root/name,code=2)
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in protected.items());checks+=1
print(f'{checks} native skeletal profile CLI/desktop checks; inputs unchanged')
