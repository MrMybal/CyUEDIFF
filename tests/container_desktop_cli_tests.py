"""Native Pak/IoStore browsing, explicit Zen reads, CLI parity and immutable source files."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
exe, cli, container_fixture, zen_fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
env = {key.upper() if os.name == 'nt' else key:value for key,value in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-container-desktop-',dir=Path.cwd()) as tmp:
    root=Path(tmp); env['LOCALAPPDATA']=str(root/'profile')
    pak_dir=root/'pak';zen_dir=root/'cooked'
    subprocess.run([container_fixture,'--write-fixtures',str(pak_dir)],env=env,check=True,capture_output=True,timeout=20)
    subprocess.run([zen_fixture,'--write-fixtures',str(zen_dir)],env=env,check=True,capture_output=True,timeout=20)
    unicode_dir=root/'conteneurs é';pak_dir.rename(unicode_dir);pak_dir=unicode_dir
    originals={p:hashlib.sha256(p.read_bytes()).hexdigest() for folder in (pak_dir,zen_dir) for p in folder.iterdir()}
    def run(*args,code=0):
        global checks
        report=root/'report.json';report.unlink(missing_ok=True)
        p=subprocess.run([exe,'--smoke','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
        assert p.returncode==code,p.stderr.decode('utf-8','replace')
        checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    pak=pak_dir/'plain.pak'
    report=run('--container',pak,'--locale','fr')
    assert report['document']['container']['metadata']['entryCount']==3 and report['document']['container']['inspection'] is None
    assert report['containerView']['visibleMembers']==3 and report['containerView']['tab']==0
    checks+=1
    report=run('--container',pak,'--container-filter','B.bin')
    assert report['containerView']['visibleMembers']==1 and report['containerView']['selectedMember']=='Content/B.bin'
    checks+=1
    report=run('--container',pak,'--container-filter','missing-query')
    assert report['containerView']['visibleMembers']==0 and report['containerView']['selectedMember'] is None
    checks+=1
    report=run('--container',pak,'--codec',root/'not-loaded.dll')
    assert report['document']['container']['inspection'] is None
    checks+=1
    report=run('--container',pak,'--container-entry','Content/A.bin')
    inspected=report['document']['container']['inspection']
    assert inspected['decodedSha256']==hashlib.sha256(b'abcdef').hexdigest() and inspected['prefixHex']=='61 62 63 64 65 66'
    assert report['containerView']['tab']==1 and report['document']['container']['zen'] is None
    checks+=1
    report=run('--container',pak_dir/'test.utoc','--container-entry','Content/A.bin')
    assert report['document']['container']['inspection']['decodedSha256']==hashlib.sha256(b'bcdef').hexdigest()
    checks+=1
    entry='000000000000000000000001'
    base=['--container',zen_dir/'test.utoc','--container-entry',entry]
    report=run(*base)
    assert report['document']['container']['zen'] is None
    checks+=1
    context=['--cooked-profile','ue5.3','--script-objects',zen_dir/'global.utoc']
    report=run(*base,*context)
    cooked=report['document']['container']['zen']
    assert cooked['name']=='/Game/Example' and cooked['exports'][0]['class']=='/Script/Test.DemoAsset'
    assert report['containerView']['tab']==2 and report['containerView']['selectedObject']==0 and not cooked['propertiesDecoded']
    expected=subprocess.run([cli,'zen-inspect',str(zen_dir/'test.utoc'),entry,*map(str,context)],env=env,check=True,capture_output=True,timeout=30)
    assert cooked==json.loads(expected.stdout)
    checks+=1
    report=run(*base,'--cooked-profile','ue5.5',code=2)
    assert 'zen-format' in report['error']
    checks+=1
    report=run(*base,'--cooked-profile','ue5.3','--script-objects',zen_dir/'test.utoc',code=2)
    assert 'zen-entry' in report['error']
    checks+=1
    run('--container',pak,'--container-entry','missing',code=2)
    run('--container',pak,'--container-entry','Content/Gone.bin',code=2)
    report=run('--container',pak,'--container-entry','Content/A.bin','--codec',root/'missing.dll',code=2)
    assert 'codec-load' in report['error'] and report['document'] is None
    checks+=1
    run('--container',root/'missing.pak',code=2)
    for extra in [['--before','other.uasset'],['--repo','.'],['--browse'],['--view','textures'],['--cooked-profile','auto']]:
        run('--container',pak,*extra,code=2)
    run('--container-entry',entry,code=2)
    # Protect source Pak, secondary IoStore partitions and global script payload aliases.
    for source,options in [(pak,['--container',pak]),(pak_dir/'test_s1.ucas',['--container',pak_dir/'test.utoc']),
                           (zen_dir/'global.ucas',['--container',zen_dir/'test.utoc','--script-objects',zen_dir/'global.utoc'])]:
        alias=root/'alias.json';alias.unlink(missing_ok=True);os.link(source,alias)
        p=subprocess.run([exe,'--smoke',*map(str,options),'--report',str(alias)],env=env,capture_output=True,timeout=30)
        assert p.returncode==2 and hashlib.sha256(source.read_bytes()).hexdigest()==originals[source]
        checks+=1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in originals.items())
    assert not (root/'profile').exists()
    checks+=1
print(f'{checks} desktop container CLI checks passed; original containers and partitions unchanged')
