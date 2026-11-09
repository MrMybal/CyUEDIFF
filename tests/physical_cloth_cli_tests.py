"""Guard the physical CLI boundary with unrelated original certified Zen fixtures."""
from pathlib import Path
import hashlib,json,os,subprocess,sys,tempfile
exe,fixture=(str(Path(x).resolve()) for x in sys.argv[1:3])
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-physical-cli-',dir=Path.cwd()) as tmp:
    root=Path(tmp);subprocess.run([fixture,'--emit-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=30)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    def run(*args,error=None):
        global checks
        p=subprocess.run([exe,*map(str,args)],env=env,capture_output=True,timeout=30)
        assert p.returncode==(2 if error else 0),(args,p.returncode,p.stderr.decode('utf-8','replace'))
        checks+=1
        if error:
            actual=json.loads(p.stderr);assert actual['error']==error,actual
        return p
    help=run('--help').stdout.decode('utf-8')
    assert 'zen-physical-cloth-transition' in help and 'zen-export-physical-cloth' in help
    for profile,suffix in [('ue5.3',''),('ue5.5','55')]:
        common=[root/('before'+suffix+'.utoc'),'000000000000000000000001','--cooked-profile',profile,'--script-objects',root/'global.utoc','--schema',root/('hybrid-'+profile+'.json')]
        run('zen-physical-cloth',*common,error='arguments')
        run('zen-physical-cloth',*common,'--physical-cloth-profile','unknown',error='arguments')
        run('zen-physical-cloth',*common,'--physical-cloth-profile','ue5.3-windows',error='physical-cloth-selection' if profile=='ue5.3' else 'physical-cloth-profile')
    common=[root/'before.utoc','000000000000000000000001','--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'hybrid-ue5.3.json','--physical-cloth-profile','ue5.3-windows']
    for command,extra in [('zen-physical-cloth',['--after-physical-cloth-profile','ue5.3-windows']),('zen-physical-cloth-wrap',['--vertex','0']),('zen-physical-cloth-vertex',['--include-values','1']),('zen-export-physical-cloth',['--transition','down'])]:
        run(command,*common,*extra,error='arguments')
    run('zen-physical-cloth-diff',*common,error='arguments')
    for flag,value in [('--physical-cloth-profile','ue5.3-windows'),('--transition','up'),('--vertex','1'),('--include-values','1')]:
        run('inspect',root/'absent.uasset',flag,value,error='arguments')
    run('zen-physical-cloth',*common,'--export-index','999999',error='physical-cloth-selection')
    for command,extra in [('physical-cloth',['--after-physical-cloth-profile','ue5.3-windows']),('physical-cloth-wrap',['--vertex','0']),('physical-cloth-vertex',['--include-values','1']),('export-physical-cloth',['--transition','down']),('physical-cloth',['--snapshot']),('physical-cloth',['--cooked-profile','ue5.3']),('physical-cloth',['--after-layouts',root/'unknown.json'])]:
        run(command,root/'absent.uasset','--physical-cloth-profile','ue5.3-windows',*extra,error='arguments')
    run('physical-cloth-diff',root/'absent.uasset',error='arguments')
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
    checks+=1
print(str(checks)+' physical CLI context/class/argument guards passed')
