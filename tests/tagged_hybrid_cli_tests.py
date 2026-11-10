"""Tagged-prefix/native-tail descriptors through existing public commands on original fixtures."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-tagged-hybrid-cli-',dir=Path.cwd()) as folder:
    root=Path(folder);subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,capture_output=True,check=True,timeout=45)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    def run(*args,code=0):
        global checks
        p=subprocess.run([exe,*map(str,args)],env=env,capture_output=True,timeout=45);assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
        return json.loads(p.stdout if p.stdout else p.stderr)
    layouts=root/'layouts.json';identity=run('layouts',layouts)['sha256']
    for version in (0,1004,1010,1011,1012,1013):
        before=root/f'before-{version}.uasset';after=root/f'after-{version}.uasset'
        decoded=run('inspect',before,'--layouts',layouts);props=decoded['snapshot']['objects']['TestAsset']['properties']
        assert props['Native']['value']['prefix']=='tagged-properties' and props['Native']['value']['data']['properties']['Mode']['value']==7
        assert props['Native']['value']['data']['native']['samples'][1]==.5 and props['AfterNative']['value']==987
        assert decoded['layoutContexts']['beforeSha256']==identity
        changed=run('diff',before,after,'--layouts',layouts,code=1)
        paths=[x['path'] for x in changed['changes']];assert any(x.endswith('/properties/Mode/value') for x in paths) and any(x.endswith('/native/samples/1') for x in paths)
        assert not changed['layoutContexts']['contextChanged']
        run('diff',before,before,'--layouts',layouts)
        bad=run('inspect',root/f'bad-{version}.uasset','--layouts',layouts)['snapshot']['objects']['TestAsset']['properties'];assert 'value' not in bad['Native'] and 'opaque' in bad['Native'] and bad['AfterNative']['value']==987
        unbound=run('inspect',before,'--layouts',root/'empty-layouts.json')['snapshot']['objects']['TestAsset']['properties'];assert 'value' not in unbound['Native']
    d=json.loads(layouts.read_text(encoding='utf-8'));d['layouts']['OuterTail']['fields'][0]['name']='values';alternate=root/'alternate.json';alternate.write_text(json.dumps(d),encoding='utf-8')
    changed=run('diff',root/'before-1013.uasset',root/'after-1013.uasset','--layouts',layouts,'--after-layouts',alternate,code=2)
    assert changed['layoutContexts']['contextChanged'] and changed['status']=='unverified'
    assert any(x['reason']=='native-struct-definition-incompatible' for x in changed['unavailableScopes'])
    assert any(x['path'].endswith('/data/properties/Mode/value') for x in changed['changes'])
    assert not any('/data/native/samples' in x['path'] or '/data/native/values' in x['path'] for x in changed['changes'])
    run('inspect',root/'before-1013.uasset','--layouts',layouts,'--out',layouts,code=2)
    link=root/'layout-alias.json'
    try:os.link(layouts,link)
    except OSError:pass
    else:run('inspect',root/'before-1013.uasset','--layouts',layouts,'--out',link,code=2)
    for prefix in ('guess',True,None):
        d=json.loads(layouts.read_text(encoding='utf-8'));d['decoders'][0]['prefix']=prefix;bad=root/'invalid.json';bad.write_text(json.dumps(d),encoding='utf-8');run('layouts',bad,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());checks+=1
print(str(checks)+' tagged hybrid CLI checks passed')
