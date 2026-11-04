"""Cooked text/delegate/weak values, precise diffs and complete opaque fallback."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,fixture=[str(Path(p).resolve()) for p in sys.argv[1:]]
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-values-cli-',dir=Path.cwd()) as folder:
    root=Path(folder);subprocess.run([fixture,'--write-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=20)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry='000000000000000000000001';prefix='/Script/Test.Collections.'
    common=['--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'values.json']
    def run(*args,code=0):
        global checks
        r=subprocess.run([cli,*map(str,args)],env=env,capture_output=True,timeout=30)
        assert r.returncode==code,(r.returncode,r.stderr.decode('utf-8','replace'),args)
        checks+=1;return json.loads(r.stderr if code==2 else r.stdout)
    report=run('zen-inspect',root/'mixed.utoc',entry,*common)
    props=report['exports'][0]['propertyRead']['properties']
    assert props[prefix+'Lookup']['value']['source']=='Original source' and props[prefix+'Lookup']['value']['key']=='StableKey';checks+=1
    bindings=props[prefix+'Tags']['value']['bindings']
    assert len(bindings)==3 and bindings[0]==bindings[2] and bindings[1]['object'] is None;checks+=1
    assert props[prefix+'After']['value']['kind']=='weak-object-reference' and props[prefix+'After']['value']['object']['path']=='/Game/Example.Example';checks+=1
    changed=run('zen-diff',root/'mixed.utoc',entry,root/'changed.utoc',entry,*common,code=1)
    for suffix in ('/value/source','/value/bindings/0/function','/value/object/path'):
        assert any(c['path'].endswith(suffix) for c in changed['changes']);checks+=1
    assert changed['binaryChanged'] and not changed['contextChanged'];checks+=1
    run('zen-diff',root/'mixed.utoc',entry,root/'mixed.utoc',entry,*common)
    table=run('zen-inspect',root/'table.utoc',entry,*common)['exports'][0]['propertyRead']['properties'][prefix+'Lookup']['value']
    assert table['kind']=='string-table' and table['table']=='Example' and table['key']=='EntryKey' and 'source' not in table;checks+=1
    invariant=run('zen-inspect',root/'invariant.utoc',entry,*common)['exports'][0]['propertyRead']['properties'][prefix+'Lookup']['value']
    assert invariant['kind']=='culture-invariant' and invariant['source']=='Invariant source';checks+=1
    zero=run('zen-inspect',root/'zero.utoc',entry,*common)['exports'][0]['propertyRead']['properties']
    assert zero[prefix+'Lookup']['value']['source']=='' and zero[prefix+'Tags']['value']['bindings']==[] and zero[prefix+'After']['value']['object'] is None;checks+=1
    for name in ('truncated','unsupported','invalid-target'):
        opaque=run('zen-inspect',root/(name+'.utoc'),entry,*common)
        assert opaque['exports'][0]['propertyRead']['status']=='opaque' and 'properties' not in opaque['exports'][0]['propertyRead'];checks+=1
    modern=['--cooked-profile','ue5.5','--script-objects',root/'global.utoc','--schema',root/'values55.json']
    assert run('zen-inspect',root/'mixed55.utoc',entry,*modern)['propertyExportsDecoded']==2;checks+=1
    snap=run('zen-inspect',root/'mixed.utoc',entry,*common,'--snapshot')
    assert snap['objects']['/Game/Example.Example']['properties']==props;checks+=1
    catalog=run('zen-catalog',root/'mixed.utoc',*common,'--snapshot')
    assert catalog['propertiesDecoded'] and catalog['propertyExportsDecoded']==2;checks+=1
    for protected in ('values.json','values.usmap','mixed.utoc','mixed.ucas'):
        run('zen-inspect',root/'mixed.utoc',entry,*common,'--out',root/protected,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());checks+=1
print(f'{checks} cooked value CLI checks passed')
