"""Explicit per-side source policies and workspace migration through the public desktop."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-source-limits-desktop-',dir=Path.cwd()) as folder:
    root=Path(folder);env['LOCALAPPDATA']=str(root/'unused-profile');subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,capture_output=True,check=True,timeout=45)
    source=root/'budget.uasset';hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()};report=root/'report.json';workspace=root/'workspace.json'
    def run(*args,code=0):
        global checks
        report.unlink(missing_ok=True);p=subprocess.run([exe,'--smoke','--no-preferences','--no-restore-session','--no-previews','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=60)
        assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    for locale in ('en','fr'):
        first=run('--before',source,'--after',source,'--max-properties',2000000,'--after-max-properties',100000,'--locale',locale,'--document-action','view-state:'+json.dumps({'layout':{'sourceLimits':True}}),'--save-workspace',workspace)
        assert first['document']['diff']['status']=='identical' and first['document']['readLimits']['before']['maxProperties']==2000000 and first['document']['readLimits']['after']['maxProperties']==100000
        saved=json.loads(workspace.read_text(encoding='utf-8'));assert saved['schema']=='cyuediff.workspace/15' and not saved['documents'][0]['settings']['inherit_after_source_limits'] and saved['documents'][0]['viewState']['layout']['sourceLimits']
        assert run('--workspace',workspace)['document']==first['document']
    for version in range(13,14):
        legacy=json.loads(workspace.read_text(encoding='utf-8'));legacy['schema']='cyuediff.workspace/'+str(version)
        for key in ('defaults_context','after_defaults_context','defaults_object','after_defaults_object','inherit_after_defaults_context','max_properties','max_property_bytes','after_max_properties','after_max_property_bytes','inherit_after_source_limits'):legacy['documents'][0]['settings'].pop(key)
        old=root/'legacy.json';old.write_text(json.dumps(legacy),encoding='utf-8')
        upgraded=run('--workspace',old,'--save-workspace',workspace);assert 'readLimits' not in upgraded['document']
        settings=json.loads(workspace.read_text(encoding='utf-8'))['documents'][0]['settings'];assert settings['max_properties']==100000 and settings['max_property_bytes']==33554432 and settings['inherit_after_source_limits']
    physical=run('--before',source,'--view','physical-cloth','--save-workspace',workspace)
    assert json.loads(workspace.read_text(encoding='utf-8'))['documents'][0]['view']=='physical-cloth'
    assert run('--workspace',workspace)['document']==physical['document']
    unavailable=run('--before',source,'--after',source,'--after-max-properties',1,code=2);assert unavailable is None or unavailable.get('error') or unavailable.get('workspace')
    for flag,value in [('--max-properties','0'),('--max-properties','4000001'),('--max-property-bytes','268435457'),('--max-property-bytes','-1')]:run('--before',source,flag,value,code=2)
    run('--before',source,'--after-max-properties',2000000,code=2)
    run('--before',source,'--max-properties',2000000,'--save-workspace',source,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());assert not (root/'unused-profile').exists()
print(str(checks)+' source read limit desktop/workspace checks passed')
