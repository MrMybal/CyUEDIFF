"""Explicit native struct contexts, precise Zen diffs and immutable inputs."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,fixture=[str(Path(p).resolve()) for p in sys.argv[1:]]
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-native-cli-',dir=Path.cwd()) as folder:
    root=Path(folder);subprocess.run([fixture,'--write-fixtures',str(root)],check=True,capture_output=True,env=env,timeout=20)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry='000000000000000000000001';field='/Script/Test.Collections.Lookup';obj='/Game/Example.Example'
    common=['--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'native.json']
    def run(*args,code=0):
        global checks
        result=subprocess.run([cli,*map(str,args)],env=env,capture_output=True,timeout=30)
        assert result.returncode==code,(result.returncode,result.stderr.decode('utf-8','replace'),args)
        checks+=1
        return json.loads(result.stderr if code==2 else result.stdout)
    report=run('zen-inspect',root/'native.utoc',entry,*common)
    prop=report['exports'][0]['propertyRead']['properties'][field]
    assert prop['nativeLayout']=='transform-f64' and prop['value']['translation']==[1000000000000.125,2.5,3.5]
    assert prop['value']['rotation']==[0,0,0,1] and prop['value']['scale']==[1,1,1];checks+=1
    snap=run('zen-inspect',root/'native.utoc',entry,*common,'--snapshot')
    assert snap['objects'][obj]['properties'][field]==prop;checks+=1
    difference=run('zen-diff',root/'native.utoc',entry,root/'changed.utoc',entry,*common,code=1)
    assert any(c['path'].endswith('/value/translation/1') and c['before']==2.5 and c['after']==99.5 for c in difference['changes'])
    assert difference['binaryChanged'] and not difference['contextChanged'];checks+=1
    run('zen-diff',root/'native.utoc',entry,root/'native.utoc',entry,*common)
    for name in ('truncated','invalid'):
        bad=run('zen-inspect',root/(name+'.utoc'),entry,*common)
        assert bad['exports'][0]['propertyRead']['status']=='opaque' and 'properties' not in bad['exports'][0]['propertyRead'];checks+=1
    unbound=run('zen-inspect',root/'native.utoc',entry,*common,'--schema',root/'unbound.json')
    assert unbound['exports'][0]['propertyRead']['status']=='opaque' and unbound['exports'][0]['sha256']==report['exports'][0]['sha256'];checks+=1
    changed_context=run('zen-diff',root/'native.utoc',entry,root/'native.utoc',entry,*common,'--after-schema',root/'unbound.json',code=1)
    assert changed_context['contextChanged'] and not changed_context['binaryChanged'];checks+=1
    modern=['--cooked-profile','ue5.5','--script-objects',root/'global.utoc','--schema',root/'native55.json']
    assert run('zen-inspect',root/'native55.utoc',entry,*modern)['propertyExportsDecoded']==2;checks+=1
    catalog=run('zen-catalog',root/'native.utoc',*common,'--snapshot')
    assert catalog['propertiesDecoded'] and catalog['propertyExportsDecoded']==2;checks+=1
    config=json.loads((root/'native.json').read_text(encoding='utf-8'));path=root/'context \u00e9.json'
    path.write_text(json.dumps(config),encoding='utf-8')
    assert run('zen-inspect',root/'native.utoc',entry,*common,'--schema',path)['propertyExportsDecoded']==2;checks+=1
    for bindings in ({'Record':'guess'},{'Record':'vector3-f64','/Script/Test.Record':'vector3-f64'}):
        value=dict(config,nativeStructs=bindings);path.write_text(json.dumps(value),encoding='utf-8')
        assert run('zen-inspect',root/'native.utoc',entry,*common,'--schema',path,code=2)['error']=='schema-context';checks+=1
    value=dict(config,reflectedStructs=['Record']);path.write_text(json.dumps(value),encoding='utf-8')
    assert run('zen-inspect',root/'native.utoc',entry,*common,'--schema',path,code=2)['error']=='schema-context';checks+=1
    for protected in ('native.json','native.usmap','native.utoc','native.ucas'):
        run('zen-inspect',root/'native.utoc',entry,*common,'--out',root/protected,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items());checks+=1
print(f'{checks} native schema CLI checks passed')
