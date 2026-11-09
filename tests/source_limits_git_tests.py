"""Explicit source work policies across verified local Git materializations."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-source-limits-git-',dir=Path.cwd()) as folder:
    root=Path(folder);subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,capture_output=True,check=True,timeout=45)
    repo=root/'repo';repo.mkdir();source=root/'budget.uasset';(repo/source.name).write_bytes(source.read_bytes())
    companion=source.with_suffix('.uexp')
    if companion.exists():(repo/companion.name).write_bytes(companion.read_bytes())
    git=['git','-c','safe.directory='+str(repo),'-C',str(repo)]
    for args in [('init',),('config','user.name','Original fixture'),('config','user.email','fixture@example.invalid'),('add','budget.uasset'),('commit','-m','Original test asset')]:subprocess.run([*git,*args],env=env,capture_output=True,check=True,timeout=30)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in repo.rglob('*') if p.is_file()}
    def run(*args,code=0):
        global checks
        p=subprocess.run([exe,*map(str,args)],env=env,capture_output=True,timeout=60)
        assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
        return json.loads(p.stdout or p.stderr)
    decoded=run('git-inspect',repo,'HEAD','budget.uasset','--max-properties',2000000)
    assert decoded['sourceReadLimits']['maxProperties']==2000000
    same=run('git-diff',repo,'HEAD','HEAD','budget.uasset','--max-properties',2000000,'--after-max-properties',100000)
    assert same['status']=='identical' and same['readLimits']['after']['maxProperties']==100000
    failed=run('git-diff',repo,'HEAD','HEAD','budget.uasset','--after-max-properties',1,code=2)
    assert failed['status']=='unverified' and not failed['comparison']['changes']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());checks+=1
print(str(checks)+' source read limit Git checks passed')
