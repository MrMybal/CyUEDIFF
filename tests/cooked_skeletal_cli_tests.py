"""Original inline skeletal fixtures: skinning, diffs, context and protected inputs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:3]]
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-skeletal-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], check=True, env=env, capture_output=True, timeout=20)
    originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    common = ['--cooked-profile', 'ue5.3', '--script-objects', str(root/'global.utoc'), '--schema', str(root/'context.json')]
    native = ['--skeletal-profile', 'ue5.3-windows', '--keep-mobile-min-lod', '0', '--has-vertex-colors', '0']
    entry = '000000000000000000000001'
    def run(command, *arguments, code=0, profile=True):
        global checks
        p = subprocess.run([cli, command, *common, *(native if profile else []), *map(str, arguments)], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, p.stderr.decode('utf-8', 'replace')
        checks += 1
        return json.loads(p.stdout) if code != 2 and p.stdout else p.stderr.decode('utf-8', 'replace')
    inline, changed, corrupt = [root/(name+'.utoc') for name in ('inline', 'changed', 'corrupt')]
    decoded = run('zen-skeletal', inline, entry)
    g = decoded['lods'][0]['geometry']
    assert len(g['vertices']) == 3 and len(g['skin']['bones']) == 2
    assert g['skin']['vertices']['0']['default'] == {'PF_DXT1': 65535}
    assert decoded['tail']['decoded'] is False and decoded['tail']['bytes'] == 1
    checks += 3
    assert run('zen-skeletal-diff', inline, entry, inline, entry)['status'] == 'identical'
    diff = run('zen-skeletal-diff', inline, entry, changed, entry, code=1)
    assert diff['status'] == 'changed' and diff['beforeMesh']['packageSha256'] != diff['afterMesh']['packageSha256']
    assert diff['beforeMesh']['schemaContextSha256'] == diff['afterMesh']['schemaContextSha256']
    checks += 3
    for command, args in [('zen-skeletal', [inline, entry]), ('zen-skeletal-diff', [inline, entry, inline, entry])]:
        assert 'cooked-skeletal-profile' in run(command, *args, profile=False, code=2)
    assert 'container-integrity' in run('zen-skeletal', corrupt, entry, code=2)
    run('zen-skeletal', inline, entry, '--skeletal-profile', 'bad', code=2)
    run('zen-skeletal', inline, entry, '--has-vertex-colors', '2', code=2)
    run('zen-skeletal', inline, entry, '--after-has-vertex-colors', '0', code=2)
    run('zen-skeletal-diff', inline, entry, changed, entry, '--after-export-index', '2', code=2)
    run('zen-skeletal', inline, entry, '--out', inline.with_suffix('.ucas'), code=2)
    run('zen-skeletal', inline, entry, '--out', root/'native.usmap', code=2)
    run('zen-skeletal', inline, entry, '--out', root/'global.ucas', code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in originals.items())
    checks += 1
print(f'{checks} cooked skeletal CLI checks passed')
