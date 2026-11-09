"""Explicit finite source policies, per-side provenance and protected inputs."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-source-limits-cli-',dir=Path.cwd()) as folder:
    root=Path(folder);subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,capture_output=True,check=True,timeout=45)
    source=root/'budget.uasset';hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    def run(*args,code=0):
        global checks
        p=subprocess.run([exe,*map(str,args)],env=env,capture_output=True,timeout=45)
        assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
        return json.loads(p.stdout or p.stderr) if p.stderr or p.stdout else None
    default=run('inspect',source);assert 'sourceReadLimits' not in default and 'readLimits' not in default
    large=run('inspect',source,'--max-properties',2000000);assert large['snapshot']==default['snapshot'] and large['sourceReadLimits']['maxProperties']==2000000
    assert large['readLimits']['before']==large['readLimits']['after']
    low=run('inspect',source,'--max-properties',1);assert not low['snapshot']['objects']['TestAsset']['properties']
    tiny=run('inspect',source,'--max-property-bytes',1);assert not tiny['snapshot']['objects']['TestAsset']['properties']
    same=run('diff',source,source,'--max-properties',2000000,'--after-max-properties',100000);assert same['status']=='identical' and same['readLimits']['before']['maxProperties']!=same['readLimits']['after']['maxProperties']
    unavailable=run('diff',source,source,'--after-max-properties',1,code=2);assert unavailable['sourceReadLimitFailed'] and unavailable['status']=='unverified' and not unavailable['changes'] and unavailable['binaryChanged'] is False
    for flag,values in [('--max-properties',('0','-1','2.5','true','4000001','18446744073709551616')),('--max-property-bytes',('0','-1','268435457','1x'))]:
        for value in values:run('inspect',source,flag,value,code=2)
    run('inspect',source,'--after-max-properties',2000000,code=2)
    run('animation-pose',source,source,'--after-max-properties',2000000,code=2)
    run('layouts',source,'--max-properties',2000000,code=2)
    run('source-types',source,522,1009,'Demo','--max-properties',2000000,code=2)
    run('inspect',source,'--max-properties',2000000,'--out',source,code=2)
    run('textconv',source,'--max-properties',2000000)
    run('textconv',source,'--max-properties',1,code=2)
    snapshot=run('inspect',source,'--snapshot','--max-properties',1);assert snapshot['sourceReadLimitFailed']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());checks+=1
print(str(checks)+' source read limit CLI checks passed')
