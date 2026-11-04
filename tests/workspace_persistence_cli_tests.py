"""Workspace saves, process restarts, fresh reads and retained tabs after failed imports."""
import hashlib,json,os,shutil,subprocess,sys,tempfile
from pathlib import Path
exe,loose_fixture,container_fixture,schema_fixture=[str(Path(p).resolve()) for p in sys.argv[1:5]]
has_git=sys.argv[5]=='ON'
env={k.upper():v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-workspace-persistence-',dir=Path.cwd()) as tmp:
    root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
    for binary,folder in [(loose_fixture,'loose'),(container_fixture,'pak'),(schema_fixture,'schema')]:
        subprocess.run([binary,'--write-fixtures',str(root/folder)],env=env,check=True,capture_output=True,timeout=20)
    before=root/'loose/Before.uasset';after=root/'loose/After.uasset';pak=root/'pak/plain.pak';schema=root/'schema'
    workspace=root/'espace \u00e9.json';report=root/'report.json';elsewhere=root/'elsewhere';elsewhere.mkdir()
    def run(*args,code=0,cwd=root):
        global checks
        report.unlink(missing_ok=True)
        p=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,cwd=cwd,capture_output=True,timeout=45)
        assert p.returncode==code,(p.returncode,p.stderr.decode('utf-8','replace'),args)
        checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    def actions(*values):return sum((['--document-action',str(v)] for v in values),[])
    source=['--before',before,'--after',after,'--no-previews','--view','comparison','--new-document','--container',pak,'--container-entry','Content/A.bin','--previews',
            '--new-document','--container',schema/'schema.utoc','--container-entry','000000000000000000000001','--cooked-profile','ue5.3',
            '--script-objects',schema/'global.utoc','--schema',schema/'schema.json','--container-tab','schema']
    original=run('--save-workspace',workspace,'--document-index','1',*source)
    saved=json.loads(workspace.read_text(encoding='utf-8'))
    assert saved['schema']=='cyuediff.workspace/1' and saved['active']==1 and len(saved['documents'])==3
    assert saved['documents'][0]['view']=='comparison' and saved['documents'][2]['containerTab']==4
    assert not saved['documents'][0]['settings']['previews'] and saved['documents'][1]['settings']['previews']
    assert all('codec' not in d['settings'] for d in saved['documents']) and original['workspaceFile']['saved']
    checks+=1
    restored=run('--workspace',workspace,cwd=elsewhere)
    assert restored['workspaceFile']['loaded'] and not restored['workspaceFile']['saved']
    assert restored['workspace']['active']==2 and restored['workspace']['closedDocuments']==0
    assert [d['document'] for d in original['workspace']['documents']]==[d['document'] for d in restored['workspace']['documents']]
    assert restored['workspaceFile']['context']==saved
    checks+=1
    reselected=run('--workspace',workspace,'--document-index','0')
    assert reselected['workspace']['active']==1 and reselected['document']['diff']['status']=='changed'
    checks+=1
    overridden=run('--workspace',workspace,'--previews')
    assert all(d['settings']['previews'] for d in overridden['workspaceFile']['context']['documents'])
    checks+=1
    copy=root/'copy.json';run('--workspace',workspace,'--save-workspace',copy)
    assert json.loads(copy.read_text(encoding='utf-8'))==saved;checks+=1
    first=before.read_bytes();before.write_bytes(after.read_bytes())
    reread=run('--workspace',workspace,'--document-index','0')
    assert reread['document']['diff']['status']=='identical';checks+=1
    before.unlink()
    missing=run('--workspace',workspace,code=2)
    assert missing['workspace']['documents'][0]['error'] and all(d['document'] is not None for d in missing['workspace']['documents'][1:])
    checks+=1;before.write_bytes(first)
    resumed=run('--container',pak,*actions('workspace-open:'+str(workspace)))
    assert len(resumed['workspace']['documents'])==3 and resumed['workspaceFile']['loaded'] and resumed['workspace']['active']==3
    checks+=1
    via=root/'actions.json'
    active=run('--container',pak,*actions('workspace-save:'+str(via),'new','workspace-open:'+str(via)))
    assert len(active['workspace']['documents'])==1 and active['workspace']['closedDocuments']==0 and active['workspaceFile']['loaded']
    checks+=1
    bad=root/'invalid.json';bad.write_text('{',encoding='utf-8')
    retained=run('--container',pak,*actions('workspace-open:'+str(bad)),code=2)
    assert len(retained['workspace']['documents'])==1 and retained['document']['container']['metadata']['kind']=='pak' and retained['workspace']['documents'][0]['revision']==1
    checks+=1
    fake_git=json.loads(workspace.read_text(encoding='utf-8'));fake_git['documents'][0]['settings']['source']=1
    git_file=root/'git-build.json';git_file.write_text(json.dumps(fake_git),encoding='utf-8')
    if not has_git:
        retained=run('--container',pak,*actions('workspace-open:'+str(git_file)),code=2)
        assert len(retained['workspace']['documents'])==1 and retained['document']['container']['metadata']['kind']=='pak';checks+=1
        run('--workspace',git_file,code=2)
    for path in [bad,root/'missing.json']:
        run('--workspace',path,code=2)
    run('--workspace',workspace,'--before',before,code=2)
    run('--workspace',workspace,'--new-document',code=2)
    run('--workspace',workspace,'--document-index','7',code=2)
    p=subprocess.run([exe,'--save-workspace',str(copy)],env=env,capture_output=True,timeout=15)
    assert p.returncode==2;checks+=1
    before_hash=hashlib.sha256(before.read_bytes()).hexdigest()
    for input_path in [before,schema/'schema.json',schema/'test.usmap',schema/'global.ucas',root/'pak/test_s1.ucas']:
        digest=hashlib.sha256(input_path.read_bytes()).hexdigest();alias=root/'alias.json';alias.unlink(missing_ok=True);os.link(input_path,alias)
        run('--save-workspace',alias,*source,code=2)
        assert hashlib.sha256(input_path.read_bytes()).hexdigest()==digest;checks+=1
    original_workspace=workspace.read_bytes()
    p=subprocess.run([exe,'--smoke','--workspace',str(workspace),'--report',str(workspace)],env=env,capture_output=True,timeout=15)
    assert p.returncode==2 and workspace.read_bytes()==original_workspace;checks+=1
    run('--workspace',workspace,'--save-workspace',workspace)
    assert workspace.read_bytes()==original_workspace;checks+=1
    p=subprocess.run([exe,'--smoke','--workspace',str(workspace),'--report',str(copy),'--save-workspace',str(copy)],env=env,capture_output=True,timeout=15)
    assert p.returncode==2;checks+=1
    p=subprocess.run([exe,'--smoke','--workspace',str(workspace),'--preferences',str(workspace),'--save-workspace',str(workspace)],env=env,capture_output=True,timeout=15)
    assert p.returncode==2 and workspace.read_bytes()==original_workspace;checks+=1
    empty=root/'empty.json';run('--save-workspace',empty)
    assert run('--workspace',empty)['document'] is None;checks+=1
    if has_git:
        repo=root/'repo';repo.mkdir();(repo/'Content').mkdir()
        def git(*args):return subprocess.run(['git','-C',str(repo),*args],env=env,capture_output=True,check=True,timeout=20).stdout.decode('utf-8').strip()
        git('init','-q');git('config','user.name','Workspace fixture');git('config','user.email','fixture@example.invalid')
        shutil.copyfile(before,repo/'Content/A.uasset');git('add','.');git('commit','-qm','Before');old=git('rev-parse','HEAD')
        shutil.copyfile(after,repo/'Content/A.uasset');git('add','.');git('commit','-qm','After');new=git('rev-parse','HEAD')
        gws=root/'history.json'
        a=run('--save-workspace',gws,'--repo',repo,'--before-revision',old,'--after-revision',new,'--before','Content/A.uasset','--no-previews')
        b=run('--workspace',gws)
        assert a['document']==b['document'] and b['document']['diff']['status']=='changed';checks+=1
        files={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in repo.rglob('*') if p.is_file()}
        run('--workspace',gws,'--save-workspace',repo/'.git/blocked.json',code=2)
        assert not (repo/'.git/blocked.json').exists() and all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in files.items());checks+=1
    assert hashlib.sha256(before.read_bytes()).hexdigest()==before_hash and not (root/'unused-profile').exists();checks+=1
print(f'{checks} workspace persistence CLI checks passed; restarts and guarded outputs validated')
