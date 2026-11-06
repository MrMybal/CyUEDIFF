"""Native structure layout selection, typed cooked views and workspace restoration."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
exe,fixture,schema_fixture=[str(Path(p).resolve()) for p in sys.argv[1:]]
env={k.upper():v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-layout-desktop-',dir=Path.cwd()) as tmp:
    root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
    for generator in (fixture,schema_fixture):
        subprocess.run([generator,'--emit-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=30)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    report=root/'report.json';workspace=root/'workspace.json'
    def run(*args,code=0):
        global checks
        report.unlink(missing_ok=True)
        result=subprocess.run([exe,'--smoke','--no-preferences','--no-previews','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
        assert result.returncode==code,(result.returncode,result.stderr.decode('utf-8','replace'),args)
        checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    base=['--before',root/'before.uasset','--after',root/'after.uasset','--layouts',root/'layouts.json','--view','properties']
    first=run(*base,'--save-workspace',workspace)
    assert first['document']['layoutContexts']['beforeSha256']==first['document']['layoutContexts']['afterSha256']
    assert any(c.get('before')==42 and c.get('after')==44 for c in first['document']['diff']['changes'])
    restored=run('--workspace',workspace,'--locale','fr')
    assert restored['document']==first['document'] and restored['workspace']['documents'][0]['viewState']==first['workspace']['documents'][0]['viewState']
    saved=json.loads(workspace.read_text(encoding='utf-8'))
    assert saved['schema']=='cyuediff.workspace/5' and saved['documents'][0]['settings']['inherit_after_layouts']
    alternate=root/'alternate.json'
    value=json.loads((root/'layouts.json').read_text(encoding='utf-8'))
    value['layouts']['Record']['fields'][0]['name']='renamed'
    alternate.write_text(json.dumps(value),encoding='utf-8')
    paired=run(*base,'--after-layouts',alternate,'--save-workspace',workspace)
    assert paired['document']['layoutContexts']['contextChanged'] and 'renamed' in json.dumps(paired['document']['diff'])
    assert run('--workspace',workspace)['document']==paired['document']
    disabled=run(*base,'--no-after-layouts','--save-workspace',workspace)
    assert disabled['document']['layoutContexts']['afterSha256']==''
    assert run('--workspace',workspace)['document']==disabled['document']
    entry='000000000000000000000001'
    for profile,suffix in [('ue5.3',''),('ue5.5','55')]:
        common=['--container',root/f'native{suffix}.utoc','--container-entry',entry,'--cooked-profile',profile,
                '--script-objects',root/'global.utoc','--schema',root/f'described-{profile}.json','--cooked-view','properties']
        cooked=run(*common)
        assert cooked['containerView']['objectProperties']['available']
        prop=cooked['document']['container']['zen']['exports'][0]['propertyRead']['properties']['/Script/Test.Collections.Lookup']
        assert prop['binaryLayout']=='Transform' and prop['value']['translation'][0]==1000000000000.125
        paired=run(*common,'--after-container',root/f'changed{suffix}.utoc','--after-cooked-profile',profile,
                   '--after-script-objects',root/'global.utoc','--after-schema',root/f'described-{profile}.json','--save-workspace',workspace)
        assert any(c['path'].endswith('/value/translation/1') for c in paired['document']['container']['selectedComparison']['zen']['changes'])
        assert run('--workspace',workspace,'--locale','fr')['document']==paired['document']
        run(*common,'--layouts',root/'layouts.json',code=2)
    run(*base,'--save-workspace',root/'layouts.json',code=2)
    run(*base,'--report',root/'layouts.json',code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items())
    assert not (root/'unused-profile').exists()
print(f'{checks} declarative layout desktop checks passed')
