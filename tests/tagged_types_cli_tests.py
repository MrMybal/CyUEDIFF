"""Explicit source type metadata preparation/selection, without Engine code execution."""
from name_key_fixture import name_key_id, key_token
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-tagged-types-cli-',dir=Path.cwd()) as folder:
    root=Path(folder);subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,capture_output=True,check=True,timeout=45)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    def run(*args,code=0):
        global checks
        p=subprocess.run([exe,*map(str,args)],env=env,capture_output=True,timeout=45)
        assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
        return json.loads(p.stderr if p.stderr else p.stdout) if p.stderr or p.stdout else None
    generated=run('source-types',root/'types.usmap',522,1004,'DemoAsset')
    assert set(generated)=={'format','layouts','taggedTypes'} and generated['taggedTypes']['schemas']['DemoAsset']['properties']['Samples']['arraySize']==2
    prepared=root/'prepared.json';run('source-types',root/'types.usmap',522,1004,'DemoAsset','--layouts',root/'base.json','--out',prepared)
    assert 'layoutContexts' not in json.loads(prepared.read_text(encoding='utf-8'))
    run('layouts',prepared)
    for version in ('-1','1x','2147483648',''):
        run('source-types',root/'types.usmap',522,version,'DemoAsset',code=2)
    run('source-types',root/'types.usmap',522,1004,'Missing',code=2)
    run('source-types',root/'types.usmap',522,1004,'DemoAsset','--layouts',root/'types.json',code=2)
    run('source-types',root/'types.usmap',522,1004,'DemoAsset','--metadata',code=2)
    for target in (root/'types.usmap',root/'base.json'):
        run('source-types',root/'types.usmap',522,1004,'DemoAsset','--layouts',root/'base.json','--out',target,code=2)
    before=root/'before.uasset';after=root/'after.uasset';catalog=root/'types.json'
    decoded=run('inspect',before,'--layouts',catalog);props=decoded['snapshot']['objects']['TestAsset']['properties']
    assert props['Native']['value']['data']['properties']['Lookup']['value']['entries'][name_key_id('TailName')]['value']['data']['properties']['Marker']['value']==31
    assert decoded['sourceTypeContext']['profileMatched'] and not decoded['sourceTypeContext']['defaultsResolved']
    changed=run('diff',before,after,'--layouts',catalog,code=1)
    assert any(x['path'].endswith('/properties/Mode/value') for x in changed['changes'])
    paired=run('diff',before,after,'--layouts',catalog,'--after-layouts',root/'base.json',code=2)
    assert paired['layoutContexts']['contextChanged'] and paired['status']=='unverified' and paired['unavailableScopes']
    assert not any('/Native/value/' in row['path'] and row['op']=='remove' for row in paired['changes'])
    run('inspect',before,'--layouts',catalog,'--out',catalog,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());checks+=1
print(str(checks)+' tagged type CLI checks passed')
