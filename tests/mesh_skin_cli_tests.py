"""Exercise native source skin geometry, semantic weight diffs and virtualized resources."""
import hashlib,json,subprocess,sys,tempfile
from pathlib import Path
exe,fixture=map(lambda p:str(Path(p).resolve()),sys.argv[1:])
checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-skin-cli-',dir=Path.cwd()) as tmp:
    root=Path(tmp)
    subprocess.run([fixture,'--write-fixtures',str(root)],capture_output=True,check=True,timeout=20)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
    def run(*args,code=0):
        global checks
        result=subprocess.run([exe,*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=30)
        assert result.returncode==code,(result.returncode,result.stdout,result.stderr,args)
        checks+=1
        return json.loads(result.stdout or result.stderr)
    before=root/'skin-before.uasset';after=root/'skin-after.uasset';virtual=root/'skin-virtualized.uasset'
    metadata=run('inspect',before)
    assert len(metadata['meshes']['sources'])==1 and metadata['coverage']['TestAsset']['nativeCoverage']=='mesh-description-source'
    geometry=run('geometry',before)
    assert geometry['skin']['bones']['arm']['parent']=='root'
    assert geometry['skin']['vertices']['0']['Default']=={'arm':32767,'root':32768}
    assert geometry['skin']['profiles']==['Cloth','Default']
    diff=run('mesh-diff',before,after,code=1)
    assert any(c['path']=='/skin/vertices/0/Default/root' and c['before']==32768 and c['after']==32767 for c in diff['changes'])
    assert run('mesh-diff',before,before)['status']=='identical'
    assert run('geometry',virtual,'--payload-root',root/'payloads')==geometry
    assert run('mesh-diff',before,virtual,'--after-payload-root',root/'payloads')['status']=='identical'
    assert run('geometry',root/'skin-invalid.uasset',code=2)['error']=='mesh-skin-reference'
    run('export-mesh',before,'--out',root/'mesh.obj')
    assert sum(line.startswith('f ') for line in (root/'mesh.obj').read_text().splitlines())==1
    for path,digest in hashes.items():assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
print(f'{checks} source skin CLI checks passed')
