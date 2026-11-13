"""Multiple native documents, isolated state, queue settlement and inactive input protection."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,workspace_fixture,container_fixture,schema_fixture=[str(Path(p).resolve()) for p in sys.argv[1:]]
env={k.upper():v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-workspace-desktop-',dir=Path.cwd()) as tmp:
    root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
    for binary,folder in [(workspace_fixture,'loose'),(container_fixture,'pak'),(schema_fixture,'schema')]:
        subprocess.run([binary,'--write-fixtures',str(root/folder)],env=env,check=True,capture_output=True,timeout=20)
    original_folder=root/'schema';unicode_folder=root/'sch\u00e9mas \u00e9';original_folder.rename(unicode_folder)
    schema=unicode_folder;pak=root/'pak/plain.pak';before=root/'loose/Before.uasset';after=root/'loose/After.uasset'
    sources={p:hashlib.sha256(p.read_bytes()).hexdigest() for directory in [root/'loose',root/'pak',schema] for p in directory.iterdir()}
    def run(*args,code=0):
        global checks
        output=root/'report.json';output.unlink(missing_ok=True)
        p=subprocess.run([exe,'--smoke','--report',str(output),*map(str,args)],env=env,capture_output=True,timeout=45)
        assert p.returncode==code,p.stderr.decode('utf-8','replace')
        checks+=1
        return json.loads(output.read_text(encoding='utf-8')) if output.exists() else None
    # A descriptor alone opens all mounted packages without a special --browse switch.
    project = root/'loose/Project.uproject'
    project.write_text('{"FileVersion":3,"EngineAssociation":"{unresolved}"}', encoding='utf-8')
    content = root/'loose/Content'
    content.mkdir()
    for name, source in [('AnyPluginType.uasset', before), ('AnyMap.umap', after)]:
        (content/name).write_bytes(source.read_bytes())
    for entry in ([str(project)], ['--project', str(project)]):
        report=run(*entry, '--no-preferences', '--no-restore-session')
        assert report['browser']['open'] and report['browser']['visiblePackages']==2
        assert report['document'] is None
        checks+=1
    report=run(str(project), '--browse-select', '/Game/AnyPluginType', '--no-previews',
               '--no-preferences', '--no-restore-session')
    assert report['document'] is not None and not report['browser']['open']
    checks+=1
    run(str(project), '--project', str(project), code=2)
    first=['--before',before,'--after',after,'--no-previews']
    second=['--new-document','--container',pak,'--container-entry','Content/A.bin']
    third=['--new-document','--container',schema/'schema.utoc','--container-entry','000000000000000000000001',
           '--cooked-profile','ue5.3','--script-objects',schema/'global.utoc','--schema',schema/'schema.json']
    report=run(*first,*second,*third)
    docs=report['workspace']['documents']
    assert len(docs)==3 and report['workspace']['active']==docs[2]['id'] and report['document']==docs[2]['document']
    assert docs[0]['document']['diff']['status']=='changed' and docs[1]['document']['container']['inspection']['decodedBytes']==6
    assert docs[2]['document']['container']['zen']['propertyExportsDecoded']==2
    assert all(d['revision']==1 and not d['busy'] and not d['error'] for d in docs)
    checks+=1
    report=run('--document-index','0',*first,*second,*third)
    docs=report['workspace']['documents']
    assert docs[0]['active'] and report['document']['diff']['status']=='changed'
    checks+=1
    report=run('--document-index','1',*first,*second,*third,'--container-filter','missing')
    assert report['workspace']['documents'][1]['active'] and report['containerView']['selectedMember']=='Content/A.bin'
    checks+=1
    report=run('--container',pak,'--new-document','--container',pak,'--container-entry','missing',code=2)
    docs=report['workspace']['documents']
    assert docs[0]['document'] is not None and docs[1]['document'] is None and docs[1]['error']
    checks+=1
    report=run('--document-index','0','--container',pak,'--new-document','--container',pak,'--container-entry','missing',code=2)
    assert report['document']['container']['metadata']['kind']=='pak' and report['workspace']['documents'][1]['error']
    checks+=1
    report=run('--container',pak,*sum((['--new-document','--container',pak] for _ in range(7)),[]))
    assert len(report['workspace']['documents'])==8
    checks+=1
    run('--container',pak,*sum((['--new-document','--container',pak] for _ in range(8)),[]),code=2)
    run('--document-index','1','--container',pak,code=2)
    run('--document-index','wrong','--container',pak,code=2)
    run('--container',pak,'--new-document','--locale','fr',code=2)
    run('--container',pak,'--new-document','--report',root/'other.json',code=2)
    def actions(*values):
        return sum((['--document-action',value] for value in values),[])
    report=run(*actions('select:0','select:1','member:Content/B.bin','select:2','filter:missing-query','select:1'),*first,*second,*third)
    docs=report['workspace']['documents']
    assert docs[1]['active'] and docs[1]['containerSelection']=='Content/B.bin' and docs[2]['visibleMembers']==0
    assert all(d['revision']==1 for d in docs) and all(a['ok'] for a in report['workspace']['actions'])
    checks+=1
    report=run(*actions('duplicate'),'--container',pak,'--container-entry','Content/A.bin')
    docs=report['workspace']['documents']
    assert len(docs)==2 and docs[1]['active'] and docs[0]['id']!=docs[1]['id'] and docs[0]['document']==docs[1]['document']
    checks+=1
    report=run(*actions('close','reopen'),*first,*second)
    docs=report['workspace']['documents']
    assert len(docs)==2 and docs[1]['id']==3 and docs[1]['container']==str(pak) and docs[1]['revision']==1
    assert report['workspace']['closedDocuments']==0
    checks+=1
    report=run(*actions('close','reopen'),'--container',pak)
    docs=report['workspace']['documents']
    assert len(docs)==2 and docs[0]['document'] is None and docs[1]['id']==3 and docs[1]['document'] is not None
    checks+=1
    report=run(*actions('select:0','close-others'),*first,*second,*third)
    assert len(report['workspace']['documents'])==1 and report['workspace']['closedDocuments']==2 and report['document']['diff']['status']=='changed'
    checks+=1
    report=run(*actions('select:0','close-others','reopen'),*first,*second,*third)
    assert report['document']['container']['zen']['propertyExportsDecoded']==2 and report['workspace']['closedDocuments']==1
    checks+=1
    report=run(*actions('reload'),'--container',pak)
    assert report['workspace']['documents'][0]['revision']==2
    checks+=1
    report=run(*actions('next','previous'),*first,*second,*third)
    assert report['workspace']['active']==3 and report['workspace']['actions'][0]['after']==1
    checks+=1
    report=run(*actions('member:Content/B.bin','new','close'),'--container',pak)
    assert len(report['workspace']['documents'])==1 and report['containerView']['selectedMember']=='Content/B.bin'
    checks+=1
    report=run(*actions('select:7'),'--container',pak,code=2)
    assert report['workspace']['actions'][0]['ok'] is False and report['document']['container']['metadata']['kind']=='pak'
    checks+=1
    report=run(*actions('reopen'),'--container',pak,code=2)
    assert report['workspace']['closedDocuments']==0
    checks+=1
    report=run(*actions('duplicate'),'--container',pak,*sum((['--new-document','--container',pak] for _ in range(7)),[]),code=2)
    assert len(report['workspace']['documents'])==8
    checks+=1
    run('--document-action','unknown','--container',pak,code=2)
    for source in [before,schema/'schema.json',schema/'test.usmap',schema/'global.ucas',root/'pak/test_s1.ucas']:
        alias=root/'alias.json';alias.unlink(missing_ok=True);os.link(source,alias)
        args=['--container',pak,'--new-document','--before',before,*third,
              '--new-document','--container',root/'pak/test.utoc']
        p=subprocess.run([exe,'--smoke','--report',str(alias),*map(str,args)],env=env,capture_output=True,timeout=30)
        assert p.returncode==2 and hashlib.sha256(source.read_bytes()).hexdigest()==sources[source]
        checks+=1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in sources.items()) and not (root/'unused-profile').exists()
    checks+=1
print(f'{checks} desktop workspace CLI checks passed; all original input hashes unchanged')
