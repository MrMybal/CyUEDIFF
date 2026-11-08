"""Exact per-platform value contexts, named cooked diffs and malformed marker coverage."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-per-platform-cli-',dir=Path.cwd()) as temp:
    root=Path(temp)
    subprocess.run([fixture,'--write-fixtures',temp],check=True,capture_output=True,timeout=20)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    def run(*args,code=0):
        global checks
        result=subprocess.run([cli,*map(str,args)],capture_output=True,timeout=30)
        assert result.returncode==code,result.stderr.decode('utf-8','replace')
        checks+=1;return json.loads(result.stdout)
    for name,expected in [('per-platform-float-cooked',1.25),('per-platform-int-cooked',1),('per-platform-bool-cooked',True)]:
        options=['--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/(name+'.json')]
        before=root/(name+'-before.utoc');after=root/(name+'-after.utoc');bad=root/(name+'-invalid.utoc')
        data=run('zen-inspect',before,'000000000000000000000001',*options)
        read=data['exports'][0]['propertyRead']
        assert read['status']=='decoded' and read['consumedBytes']==10
        assert read['properties']['/Script/Test.Collections.Lookup']['value']=={'platformValue':expected}
        checks+=2
        delta=run('zen-diff',before,'000000000000000000000001',after,'000000000000000000000001',*options,code=1)
        assert any(change['path'].endswith('/platformValue') and change['before']==expected for change in delta['changes'])
        checks+=1
        assert run('zen-diff',before,'000000000000000000000001',before,'000000000000000000000001',*options)['status']=='identical'
        invalid=run('zen-inspect',bad,'000000000000000000000001',*options)
        assert invalid['propertyExportsFailed']==1 and invalid['exports'][0]['propertyRead']['error']=='schema-value'
        assert invalid['exports'][0]['propertyRead']['status']=='opaque'
        checks+=2
    assert all(hashlib.sha256(path.read_bytes()).hexdigest()==value for path,value in hashes.items())
    checks+=1
print(f'{checks} per-platform CLI checks passed; original fixtures unchanged')
