"""Native worker/UI acceptance of prepared effective defaults and persisted per-side contexts."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=map(Path,sys.argv[1:3]);checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-effective-desktop-',dir=Path.cwd()) as folder:
 root=Path(folder);env={k.upper():v for k,v in os.environ.items()};env['LOCALAPPDATA']=str(root/'unused-profile')
 subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=45)
 originals={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
 before,after,named=(root/(x+'.uasset') for x in ('before','after','named'));context=root/'context.json';report=root/'report.json'
 field='/Script/Test.Child.Count'
 def run(*args,code=0):
  global checks
  report.unlink(missing_ok=True)
  p=subprocess.run([exe,'--smoke','--no-preferences','--no-restore-session','--no-previews','--report',report,*map(str,args)],env=env,capture_output=True,timeout=60)
  assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace')[:1500]);checks+=1
  return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
 def data(j):return j['document']['effectiveDefaults']
 base=['--before',before,'--after',after,'--defaults-context',context]
 for locale in ('en','fr'):
  j=run(*base,'--effective-view','changes','--locale',locale)
  d=data(j);assert d['before']['complete'] and d['after']['complete'] and d['before']['fields'][field]['value']==41 and d['after']['fields'][field]['value']==99
  assert d['comparison']['status']=='changed' and len(d['comparison']['changes'])==1 and d['presentationRows']==1
  assert j['effectiveDefaultsView']['mode']=='changes' and j['effectiveDefaultsView']['rows']==1 and j['effectiveDefaultsView']['comparisonAvailable']
 identical=run('--before',before,'--after',before,'--defaults-context',context,'--effective-view','changes')
 assert data(identical)['comparison']['status']=='identical' and data(identical)['presentationRows']==0
 hidden=run('--before',before,'--after',named,'--defaults-context',context,'--effective-view','changes')
 assert data(hidden)['comparison']['changes'][0]['after']=={'base':'Item','number':2}
 for mode in ('before','after'):
  j=run(*base,'--effective-view',mode);assert j['effectiveDefaultsView']['mode']==mode and data(j)[mode]['complete']
 single=run('--before',before,'--defaults-context',context,'--effective-view','before')
 assert data(single)['before']['complete'] and data(single)['after'] is None and data(single)['comparison'] is None
 disabled=run(*base,'--no-after-defaults-context','--effective-view','changes')
 assert data(disabled)['before']['complete'] and data(disabled)['after'] is None and data(disabled)['comparison'] is None and data(disabled)['afterError']
 wrong=run(*base,'--defaults-object','missing','--effective-view','before')
 assert data(wrong)['before'] is None and data(wrong)['beforeError'] and data(wrong)['after']['complete'] and data(wrong)['comparison'] is None
 bad=root/'bad.json';bad.write_text('{}',encoding='utf-8')
 independent=run(*base,'--after-defaults-context',bad)
 assert data(independent)['before']['complete'] and data(independent)['after'] is None and data(independent)['afterError']
 workspace=root/'workspace.json'
 configured=run(*base,'--after-defaults-context',context,'--defaults-object','TestAsset','--after-defaults-object','TestAsset','--effective-view','after','--save-workspace',workspace)
 saved=json.loads(workspace.read_text(encoding='utf-8'));s=saved['documents'][0]['settings']
 assert saved['schema']=='cyuediff.workspace/15' and not s['inherit_after_defaults_context'] and s['defaults_object']==s['after_defaults_object']=='TestAsset'
 assert saved['documents'][0]['viewState']['asset']['effectiveProperties']
 restored=run('--workspace',workspace)
 assert data(restored)==data(configured) and restored['effectiveDefaultsView']==configured['effectiveDefaultsView']
 repeated=run('--workspace',workspace,'--document-action','reload')
 assert data(repeated)==data(configured) and repeated['effectiveDefaultsView']==configured['effectiveDefaultsView'] and repeated['workspace']['documents'][0]['revision']==2
 duplicate=run('--workspace',workspace,'--document-action','duplicate')
 assert len(duplicate['workspace']['documents'])==2 and data(duplicate)==data(configured)
 if len(sys.argv)>3:
  git=sys.argv[3];repo=root/'repo';repo.mkdir()
  genv=dict(env);genv.update(GIT_AUTHOR_NAME='CyUEDiff test',GIT_AUTHOR_EMAIL='test@example.invalid',GIT_COMMITTER_NAME='CyUEDiff test',GIT_COMMITTER_EMAIL='test@example.invalid')
  def g(*args):subprocess.run([git,'-C',repo,*args],env=genv,check=True,capture_output=True,timeout=30)
  g('init');(repo/'Asset.uasset').write_bytes(before.read_bytes());g('add','Asset.uasset');g('commit','-m','before')
  (repo/'Asset.uasset').write_bytes(named.read_bytes());g('add','Asset.uasset');g('commit','-m','after')
  revision=run('--repo',repo,'--before','Asset.uasset','--before-revision','HEAD~1','--after-revision','HEAD','--defaults-context',context,'--effective-view','changes')
  assert data(revision)['before']['complete'] and data(revision)['after']['complete'] and data(revision)['comparison']['changes'][0]['after']=={'base':'Item','number':2}
 run(*base,'--effective-view','invalid',code=2)
 # Refuse a report alias of the captured snapshot, not only the manifest or source package.
 manifest=json.loads(context.read_text(encoding='utf-8'));capture=Path(manifest['snapshot']['file'])
 if not capture.is_absolute():capture=context.parent/capture
 p=subprocess.run([exe,'--smoke','--no-preferences','--no-previews',*map(str,base),'--report',capture],env=env,capture_output=True,timeout=60)
 assert p.returncode==2;checks+=1
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in originals.items())
print(checks,'desktop effective-default checks passed')
