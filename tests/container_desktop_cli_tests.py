"""Native Pak/IoStore browsing, explicit Zen reads, CLI parity and immutable source files."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
exe, cli, container_fixture, zen_fixture, schema_fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
env = {key.upper() if os.name == 'nt' else key:value for key,value in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-container-desktop-',dir=Path.cwd()) as tmp:
    root=Path(tmp); env['LOCALAPPDATA']=str(root/'profile')
    pak_dir=root/'pak';zen_dir=root/'cooked'
    subprocess.run([container_fixture,'--write-fixtures',str(pak_dir)],env=env,check=True,capture_output=True,timeout=20)
    subprocess.run([zen_fixture,'--write-fixtures',str(zen_dir)],env=env,check=True,capture_output=True,timeout=20)
    schema_dir=root/'schema'
    subprocess.run([schema_fixture,'--write-fixtures',str(schema_dir)],env=env,check=True,capture_output=True,timeout=20)
    (schema_dir/'after.usmap').write_bytes((schema_dir/'test.usmap').read_bytes())
    manifest=json.loads((schema_dir/'schema.json').read_text(encoding='utf-8'))
    (schema_dir/'after.json').write_text(json.dumps(dict(manifest,usmap='after.usmap')),encoding='utf-8')
    unicode_dir=root/'conteneurs é';pak_dir.rename(unicode_dir);pak_dir=unicode_dir
    originals={p:hashlib.sha256(p.read_bytes()).hexdigest() for folder in (pak_dir,zen_dir,schema_dir) for p in folder.iterdir()}
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
    for version in (6,7,8):
        report=run('--container',pak_dir/f'toc{version}.utoc','--container-entry','Content/A.bin')
        assert report['document']['container']['metadata']['version']==version
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
    # A background inspection must not publish bytes whose stored identity is stale.
    corrupt = root/'corrupt.utoc'
    corrupt.write_bytes((pak_dir/'test.utoc').read_bytes())
    (root/'corrupt.ucas').write_bytes(b'abXd')
    (root/'corrupt_s1.ucas').write_bytes(b'efgh')
    failed = run('--container', corrupt, '--container-entry', 'Content/A.bin', code=2)
    assert 'container-integrity' in failed['error'] and failed['document'] is None
    checks += 1
    # Pairing is metadata-only until an explicit decoded comparison or inspection.
    pair_args=['--container',pak,'--after-container',pak]
    report=run(*pair_args,'--codec',root/'not-loaded.dll','--container-changes-only')
    pairs=report['document']['container']
    assert len(pairs['members'])==2 and all(p['state']=='unchecked' for p in pairs['members'])
    assert pairs['comparison'] is None and report['containerView']['visibleMembers']==2
    checks+=1
    report=run(*pair_args,'--compare-container-bytes','--container-changes-only')
    pairs=report['document']['container']
    assert pairs['comparison']['status']=='identical' and pairs['comparison']['unchanged']==2
    assert report['containerView']['visibleMembers']==0
    checks+=1
    pair_args=['--container',pak,'--after-container',pak_dir/'test.utoc','--compare-container-bytes']
    report=run(*pair_args)
    comparison=report['document']['container']['comparison']
    expected=subprocess.run([cli,'container-diff',str(pak),str(pak_dir/'test.utoc')],env=env,capture_output=True,timeout=30)
    assert expected.returncode==1 and comparison==json.loads(expected.stdout)
    assert all(p['state']=='modified' for p in report['document']['container']['members'])
    checks+=1
    pair_args=['--container',pak,'--after-container',zen_dir/'test.utoc']
    report=run(*pair_args,'--container-entry','path:Content/A.bin')
    assert report['document']['container']['selectedComparison']['state']=='removed'
    assert report['document']['container']['after']['inspection'] is None
    assert report['containerView']['selectedKey']=='path:Content/A.bin'
    checks+=1
    report=run(*pair_args,'--container-entry','id:'+entry,'--after-cooked-profile','ue5.3')
    assert report['document']['container']['selectedComparison']['state']=='added'
    assert report['document']['container']['inspection'] is None and report['containerView']['tab']==2
    checks+=1
    pair_args=[*base,'--after-container',zen_dir/'changed.utoc',*context,
               '--after-cooked-profile','ue5.3','--after-script-objects',zen_dir/'global.utoc']
    report=run(*pair_args)
    comparison=report['document']['container']['selectedComparison']
    expected=subprocess.run([cli,'zen-diff',str(zen_dir/'test.utoc'),entry,str(zen_dir/'changed.utoc'),entry,*map(str,context)],env=env,capture_output=True,timeout=30)
    assert expected.returncode==1 and comparison['zen']==json.loads(expected.stdout) and comparison['state']=='modified'
    checks+=1
    report=run(*pair_args,'--container-tab','comparison')
    assert report['containerView']['tab']==3
    checks+=1
    run('--container',pak,'--container-tab','objects',code=2)
    run('--container',pak,'--container-tab','comparison',code=2)
    run('--container',pak,'--container-tab','unknown',code=2)
    # Equal bytes can have different interpretation contexts; preserve that evidence.
    report=run(*base,'--after-container',zen_dir/'test.utoc',*context,
               '--after-cooked-profile','ue5.3','--after-script-objects',zen_dir/'other-global.utoc')
    comparison=report['document']['container']['selectedComparison']
    assert comparison['state']=='unchanged' and comparison['zen']['contextChanged'] and comparison['zen']['status']=='changed'
    checks+=1
    report=run(*base,'--after-container',zen_dir/'changed.utoc','--cooked-profile','ue5.3')
    assert report['document']['container']['after']['zen'] is None
    assert report['document']['container']['selectedComparison']['zen'] is None
    checks+=1
    run(*base,'--after-container',zen_dir/'changed.utoc','--after-cooked-profile','ue5.5',code=2)
    for extra in [['--compare-container-bytes'],['--container-changes-only'],['--after-cooked-profile','ue5.3'],['--after-script-objects',zen_dir/'global.utoc']]:
        run('--container',pak,*extra,code=2)
    run('--after-container',pak,code=2)
    mapped_base=['--container',schema_dir/'schema.utoc','--cooked-profile','ue5.3',
                 '--script-objects',schema_dir/'global.utoc','--schema',schema_dir/'schema.json']
    report=run(*mapped_base,'--codec',root/'not-loaded.dll')
    assert report['document']['container']['schemaContext']['mappingLoaded'] is False
    checks+=1
    report=run(*mapped_base,'--container-entry',entry)
    mapped=report['document']['container']
    assert mapped['zen']['exports'][0]['propertyRead']['properties']['DemoAsset.Health']['value']==42
    assert mapped['schemaContext']['mappingLoaded'] and not mapped['schemaContext']['mappingVerified']
    expected=subprocess.run([cli,'zen-inspect',str(schema_dir/'schema.utoc'),entry,'--cooked-profile','ue5.3',
                             '--script-objects',str(schema_dir/'global.utoc'),'--schema',str(schema_dir/'schema.json')],
                            env=env,capture_output=True,timeout=30,check=True)
    assert mapped['zen']==json.loads(expected.stdout)
    checks+=1
    mapped_pair=[*mapped_base,'--container-entry',entry,'--after-container',schema_dir/'changed.utoc',
                 '--after-cooked-profile','ue5.3','--after-script-objects',schema_dir/'global.utoc',
                 '--after-schema',schema_dir/'after.json']
    report=run(*mapped_pair)
    mapped=report['document']['container']
    expected=subprocess.run([cli,'zen-diff',str(schema_dir/'schema.utoc'),entry,str(schema_dir/'changed.utoc'),entry,
                             '--cooked-profile','ue5.3','--script-objects',str(schema_dir/'global.utoc'),
                             '--schema',str(schema_dir/'schema.json'),'--after-schema',str(schema_dir/'after.json')],
                            env=env,capture_output=True,timeout=30)
    assert expected.returncode==1 and mapped['selectedComparison']['zen']==json.loads(expected.stdout)
    assert mapped['zen']['propertyExportsDecoded']==mapped['after']['zen']['propertyExportsDecoded']==2
    checks+=1
    assert report['containerView']['objectProperties']['comparing'] and report['containerView']['objectProperties']['mode']=='properties'
    report=run(*mapped_pair,'--container-tab','schema')
    assert report['containerView']['tab']==4
    checks+=1
    report=run(*mapped_pair,'--cooked-view','object')
    assert report['containerView']['objectProperties']['mode']=='object'
    checks+=1
    run('--container',pak,'--container-tab','schema',code=2)
    run('--container',pak,'--cooked-view','invalid',code=2)
    report=run(*mapped_pair[:-2])
    assert report['document']['container']['after']['zen']['propertyExportsDecoded']==0
    assert report['document']['container']['after']['schemaContext'] is None
    assert report['containerView']['objectProperties']['available'] and not report['containerView']['objectProperties']['comparing']
    checks+=1
    missing=root/'missing-mapping.json'
    missing.write_text(json.dumps(dict(manifest,usmap='absent.usmap')),encoding='utf-8')
    report=run(*mapped_base,'--schema',missing)
    assert not report['document']['container']['schemaContext']['mappingLoaded']
    checks+=1
    run(*mapped_base,'--schema',missing,'--container-entry',entry,code=2)
    invalid=root/'invalid-context.json'
    invalid.write_text(json.dumps(dict(manifest,usmap=str(schema_dir/'test.usmap'),sha256='0'*64)),encoding='utf-8')
    report=run(*mapped_base,'--schema',invalid,'--container-entry',entry,code=2)
    assert 'schema-context' in report['error']
    checks+=1
    run('--container',schema_dir/'schema.utoc','--schema',schema_dir/'schema.json',code=2)
    run(*mapped_base,'--after-container',schema_dir/'changed.utoc','--after-schema',schema_dir/'after.json',code=2)
    run('--schema',schema_dir/'schema.json',code=2)
    for source in [schema_dir/'schema.json',schema_dir/'test.usmap',schema_dir/'after.json',schema_dir/'after.usmap']:
        alias=root/'schema-alias.json';alias.unlink(missing_ok=True);os.link(source,alias)
        process=subprocess.run([exe,'--smoke',*map(str,mapped_pair),'--report',str(alias)],env=env,capture_output=True,timeout=30)
        assert process.returncode==2 and hashlib.sha256(source.read_bytes()).hexdigest()==originals[source]
        checks+=1
    # Protect source Pak, secondary IoStore partitions and global script payload aliases.
    for source,options in [(pak,['--container',pak]),(pak_dir/'test_s1.ucas',['--container',pak_dir/'test.utoc']),
                           (zen_dir/'global.ucas',['--container',zen_dir/'test.utoc','--script-objects',zen_dir/'global.utoc']),
                           (pak_dir/'test_s1.ucas',['--container',pak,'--after-container',pak_dir/'test.utoc']),
                           (zen_dir/'global.ucas',['--container',pak,'--after-container',zen_dir/'test.utoc','--after-script-objects',zen_dir/'global.utoc'])]:
        alias=root/'alias.json';alias.unlink(missing_ok=True);os.link(source,alias)
        p=subprocess.run([exe,'--smoke',*map(str,options),'--report',str(alias)],env=env,capture_output=True,timeout=30)
        assert p.returncode==2 and hashlib.sha256(source.read_bytes()).hexdigest()==originals[source]
        checks+=1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in originals.items())
    assert not (root/'profile').exists()
    checks+=1
print(f'{checks} desktop container CLI checks passed; original containers and partitions unchanged')
