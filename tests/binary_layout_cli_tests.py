"""Declarative layouts through the public CLI, with unchanged source hashes."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture, schema_fixture = [str(Path(p).resolve()) for p in sys.argv[1:4]]
with_git = len(sys.argv) > 4 and sys.argv[4] == 'git'
env = {k.upper(): v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-layout-cli-', dir=Path.cwd()) as folder:
    root = Path(folder)
    for generator in (fixture, schema_fixture):
        subprocess.run([generator, '--emit-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=30)
    originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    def run(*args, code=0):
        global checks
        result = subprocess.run([exe, *map(str, args)], env=env, capture_output=True, timeout=40)
        assert result.returncode == code, (result.returncode, args, result.stdout.decode('utf-8','replace'), result.stderr.decode('utf-8','replace'))
        checks += 1
        return json.loads(result.stderr if code == 2 else result.stdout)
    layout = root / 'layouts.json'
    report = run('layouts', layout)
    identity = report['sha256']
    assert report['decoderCount'] == 1 and report['definition']['layouts']['Record']['type'] == 'record'
    before, after = root / 'before.uasset', root / 'after.uasset'
    inspected = run('inspect', before, '--layouts', layout)
    assert inspected['snapshot']['objects']['TestAsset']['properties']['Native']['value']['data']['number'] == 42
    assert inspected['layoutContexts']['beforeSha256'] == identity
    changed = run('diff', before, after, '--layouts', layout, code=1)
    assert any(c.get('before') == 42 and c.get('after') == 44 for c in changed['changes'])
    assert not changed['layoutContexts']['contextChanged']
    run('diff', before, before, '--layouts', layout)
    run('inspect', root/'bad.uasset', '--layouts', layout)
    alternate = root / 'alternate.json'
    value = json.loads(layout.read_text(encoding='utf-8'))
    value['layouts']['Record']['fields'][0]['name'] = 'renamed'
    alternate.write_text(json.dumps(value), encoding='utf-8')
    different = run('diff', before, after, '--layouts', layout, '--after-layouts', alternate, code=1)
    assert different['layoutContexts']['contextChanged'] and 'renamed' in json.dumps(different)
    empty = root / 'empty.json'
    empty.write_text(json.dumps({'format':'cyuediff.binary-layouts/1','layouts':{}}), encoding='utf-8')
    plain = run('inspect', before)
    assert run('inspect', before, '--layouts', empty)['snapshot'] == plain['snapshot']
    for command, args in [('graph',[before]), ('layouts',[layout]), ('container',[root/'native.utoc'])]:
        assert run(command,*args,'--layouts',layout,code=2)['error'] == 'arguments'
    assert run('inspect',before,'--after-layouts',layout,code=2)['error'] == 'arguments'
    assert run('inspect',before,'--layouts',layout,'--metadata',code=2)['error'] == 'arguments'
    run('inspect',before,'--layouts',layout,'--out',layout,code=2)
    run('layouts',layout,'--out',layout,code=2)
    link=root/'layout-hardlink.json'
    try:
        os.link(layout,link)
    except OSError:
        pass
    else:
        run('inspect',before,'--layouts',layout,'--out',link,code=2)
    entry='000000000000000000000001'
    key='/Script/Test.Collections.Lookup'
    for profile,suffix in [('ue5.3',''),('ue5.5','55')]:
        context=root/f'described-{profile}.json'
        common=['--cooked-profile',profile,'--script-objects',root/'global.utoc','--schema',context]
        cooked=run('zen-inspect',root/f'native{suffix}.utoc',entry,*common)
        prop=cooked['exports'][0]['propertyRead']['properties'][key]
        assert prop['binaryLayout']=='Transform' and prop['value']['translation'][0]==1000000000000.125
        diff=run('zen-diff',root/f'native{suffix}.utoc',entry,root/f'changed{suffix}.utoc',entry,*common,code=1)
        assert any(c['path'].endswith('/value/translation/1') for c in diff['changes']) and not diff['contextChanged']
        run('zen-inspect',root/f'native{suffix}.utoc',entry,*common,'--out',context,code=2)
    if with_git:
        repo=root/'repo'
        repo.mkdir()
        def git(*args):
            return subprocess.run(['git','-C',str(repo),*map(str,args)],env=env,check=True,capture_output=True,timeout=30)
        git('init','-q','--initial-branch','main')
        git('config','user.name','CyUEDiff Tests')
        git('config','user.email','tests@example.invalid')
        git('config','commit.gpgsign','false')
        asset=repo/'Asset.uasset'
        asset.write_bytes(before.read_bytes());git('add','Asset.uasset');git('commit','-q','-m','Before')
        asset.write_bytes(after.read_bytes());git('add','Asset.uasset');git('commit','-q','-m','After')
        inspected=run('git-inspect',repo,'HEAD~1','Asset.uasset','--layouts',layout)
        assert inspected['snapshot']['objects']['TestAsset']['properties']['Native']['value']['data']['number']==42
        diff=run('git-diff',repo,'HEAD~1','HEAD','Asset.uasset','--layouts',layout,'--after-layouts',alternate,code=1)
        assert diff['layoutContexts']['contextChanged'] and 'renamed' in json.dumps(diff)
        assert asset.read_bytes()==after.read_bytes() and not git('status','--porcelain').stdout
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in originals.items())
    checks += 1
print(f'{checks} declarative layout CLI checks passed')
