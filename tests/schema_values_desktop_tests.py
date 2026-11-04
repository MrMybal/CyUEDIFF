"""Text/delegate/weak comparisons in desktop inspectors and restored workspaces."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,fixture=[str(Path(p).resolve()) for p in sys.argv[1:]]
env={k.upper():v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-values-desktop-',dir=Path.cwd()) as folder:
    root=Path(folder);env['LOCALAPPDATA']=str(root/'unused-profile')
    subprocess.run([fixture,'--write-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=20)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry='000000000000000000000001';prefix='/Script/Test.Collections.';report=root/'report.json'
    base=['--container',root/'mixed.utoc','--container-entry',entry,'--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'values.json','--cooked-view','properties']
    pair=[*base,'--after-container',root/'changed.utoc','--after-cooked-profile','ue5.3','--after-script-objects',root/'global.utoc','--after-schema',root/'values.json']
    def run(*args,code=0):
        global checks
        report.unlink(missing_ok=True)
        r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
        assert r.returncode==code,(r.returncode,r.stderr.decode('utf-8','replace'),args)
        checks+=1;return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    first=run(*pair)
    props=first['document']['container']['zen']['exports'][0]['propertyRead']['properties']
    assert props[prefix+'Lookup']['value']['source']=='Original source' and first['containerView']['objectProperties']['available'] and first['containerView']['objectProperties']['comparing'];checks+=1
    diff=first['document']['container']['selectedComparison']['zen']
    assert all(any(c['path'].endswith(suffix) for c in diff['changes']) for suffix in ('/value/source','/value/bindings/0/function','/value/object/path'));checks+=1
    selected='/~1Script~1Test.Collections.Tags/bindings/0/function'
    state={'container':{'values':{'filter':'function','selected':selected}}}
    saved=root/'workspace.json'
    viewed=run(*pair,'--document-action','view-state:'+json.dumps(state),'--save-workspace',saved)
    assert viewed['workspace']['documents'][0]['viewState']['container']['values']['selected']==selected;checks+=1
    restored=run('--workspace',saved,'--locale','fr')
    assert restored['document']==first['document'] and restored['workspace']['documents'][0]['viewState']['container']['values']['selected']==selected;checks+=1
    for name in ('unsupported','invalid-target','truncated'):
        r=run(*base,'--container',root/(name+'.utoc'))
        assert not r['containerView']['objectProperties']['available'] and r['containerView']['objectProperties']['beforeStatus']=='opaque';checks+=1
    r=run(*base,'--container',root/'zero.utoc')
    assert r['containerView']['objectProperties']['available'];checks+=1
    r=run(*base,'--container',root/'mixed55.utoc','--cooked-profile','ue5.5','--schema',root/'values55.json')
    assert r['document']['container']['zen']['propertyExportsDecoded']==2;checks+=1
    for protected in ('values.json','values.usmap','mixed.utoc','mixed.ucas'):run(*base,'--save-workspace',root/protected,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items()) and not (root/'unused-profile').exists();checks+=1
print(f'{checks} cooked value desktop checks passed')
