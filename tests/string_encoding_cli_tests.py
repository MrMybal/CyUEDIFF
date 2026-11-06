"""Verify explicit codepages against Python's codec implementation through public readers."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
cli,fixture=[str(Path(p).resolve()) for p in sys.argv[1:3]]
desktop=str(Path(sys.argv[3]).resolve()) if len(sys.argv)>3 else None
env={k.upper():v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-encoded-cli-',dir=Path.cwd()) as tmp:
    root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
    subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=30)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    expected=b'caf\xe9 \x80'.decode('cp1252')
    def run(*args,code=0):
        global checks
        r=subprocess.run([cli,*map(str,args)],env=env,capture_output=True,timeout=30)
        assert r.returncode==code,(r.returncode,args,r.stderr.decode('utf-8','replace'))
        checks+=1;return json.loads(r.stderr if code==2 else r.stdout)
    asset=root/'encoded.uasset';catalog=root/'encoded-layouts.json'
    data=run('inspect',asset,'--layouts',catalog)
    assert data['snapshot']['objects']['TestAsset']['properties']['EncodingText']['value']==expected
    other=root/'latin1.json';j=json.loads(catalog.read_text(encoding='utf-8'));j['narrowStringEncoding']='latin1';other.write_text(json.dumps(j),encoding='utf-8')
    delta=run('diff',asset,asset,'--layouts',catalog,'--after-layouts',other,code=1)
    assert delta['layoutContexts']['contextChanged'] and any(c.get('before')==expected and c.get('after')==b'caf\xe9 \x80'.decode('latin1') for c in delta['changes'])
    changed=root/'changed.uasset';changed.write_bytes(asset.read_bytes().replace(b'caf\xe9 \x80',b'caf\xe8 \x80',1))
    delta=run('diff',asset,changed,'--layouts',catalog,code=1)
    assert any(c.get('before')==expected and c.get('after')==b'caf\xe8 \x80'.decode('cp1252') for c in delta['changes'])
    entry='000000000000000000000001'
    common=['--cooked-profile','ue5.3','--script-objects',root/'encoded-global.utoc','--schema',root/'encoded-context.json']
    cooked=run('zen-inspect',root/'encoded.utoc',entry,*common)
    assert cooked['exports'][0]['propertyRead']['properties']['/Script/Test.Collections.Lookup']['value']==expected
    run('inspect',asset,'--layouts',catalog,'--out',catalog,code=2)
    if desktop:
        report=root/'report.json';workspace=root/'workspace.json'
        def app(*args,code=0):
            global checks
            report.unlink(missing_ok=True)
            r=subprocess.run([desktop,'--smoke','--no-previews','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
            assert r.returncode==code,(r.returncode,args,r.stderr.decode('utf-8','replace'))
            checks+=1;return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
        first=app('--before',asset,'--after',changed,'--layouts',catalog,'--locale','fr','--view','properties','--save-workspace',workspace)
        assert any(c.get('before')==expected for c in first['document']['diff']['changes'])
        assert app('--workspace',workspace)['document']==first['document']
        cooked=app('--container',root/'encoded.utoc','--container-entry',entry,*common,'--cooked-view','properties')
        assert cooked['containerView']['objectProperties']['available']
        assert cooked['document']['container']['zen']['exports'][0]['propertyRead']['properties']['/Script/Test.Collections.Lookup']['value']==expected
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items())
print(f'{checks} explicit encoding CLI/desktop checks passed')
