"""Independent classic tagged-types contexts and EN/FR workspace restoration."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-tagged-types-desktop-',dir=Path.cwd()) as folder:
    root=Path(folder);env['LOCALAPPDATA']=str(root/'unused-profile');subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,capture_output=True,check=True,timeout=45)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()};report=root/'report.json';workspace=root/'workspace.json'
    def run(*args,code=0):
        global checks
        report.unlink(missing_ok=True);p=subprocess.run([exe,'--smoke','--no-preferences','--no-restore-session','--no-previews','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=60)
        assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    for version in (1004,):
        for locale in ('en','fr'):
            state={'asset':{'selected':'TestAsset','propertiesAfter':False,'browseProperties':True,'browserObject':'TestAsset','browserRoot':0,'propertyBrowser':{'scope':'/Native/value/data/native/samples','first':0,'serializationFields':False,'properties':{'selected':'/Native/value/data/native/samples/1','filter':'','changedOnly':True}}}}
            args=['--before',root/'before.uasset','--after',root/'after.uasset','--layouts',root/'types.json','--view','properties','--locale',locale,'--document-action','view-state:'+json.dumps(state,separators=(',',':'))]
            first=run(*args,'--save-workspace',workspace);assert first['document']['before']['completeInput'] and not first['document']['before']['diagnostics']
            assert any(x['path'].endswith('/native/samples/1') and x['before']==.5 and x['after']==1.5 for x in first['document']['diff']['changes'])
            saved=json.loads(workspace.read_text(encoding='utf-8'));assert saved['documents'][0]['viewState']['asset']['propertyBrowser']['properties']['selected']=='/Native/value/data/native/samples/1'
            restored=run('--workspace',workspace,'--locale','fr' if locale=='en' else 'en');assert restored['document']==first['document']
    paired=run('--before',root/'before.uasset','--after',root/'after.uasset','--layouts',root/'types.json','--after-layouts',root/'base.json','--save-workspace',workspace)
    assert paired['document']['layoutContexts']['contextChanged']
    assert run('--workspace',workspace)['document']==paired['document']
    run('--before',root/'before.uasset','--layouts',root/'types.json','--save-workspace',root/'types.json',code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());assert not (root/'unused-profile').exists()
print(str(checks)+' tagged type desktop/workspace checks passed')
