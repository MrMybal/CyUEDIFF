"""Check source morph names, corner normals, diffs and virtualized CLI inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture = (str(Path(p).resolve()) for p in sys.argv[1:])
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-morph-cli-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    subprocess.run([fixture, '--write-fixtures', str(root)], capture_output=True, check=True, timeout=30)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
    def run(*args, code=0):
        global checks
        result = subprocess.run([exe, *map(str, args)], capture_output=True, text=True, encoding='utf-8', timeout=30)
        assert result.returncode == code, (result.returncode, result.stdout, result.stderr, args)
        checks += 1
        return json.loads(result.stdout or result.stderr)
    before, after, virtual = (root / f'morph-{name}.uasset' for name in ('before', 'after', 'virtualized'))
    geometry = run('geometry', before)
    targets = geometry['morphs']['targets']
    assert set(targets) == {'Smile', 'Blink'}
    assert targets['Smile']['positions'] == {'0': [1, 0, 0], '2': [0, -.5, 0], '4': [0, 0, 0]}
    assert targets['Smile']['normals']['1'] == [0, .25, 0]
    assert targets['Blink']['normals'] is None
    diff = run('mesh-diff', before, after, code=1)
    changes = {c['path']: c for c in diff['changes']}
    assert changes['/morphs/targets/Smile/positions/0/0']['after'] == 2
    assert changes['/morphs/targets/Smile/normals/1/1']['after'] == .5
    assert run('mesh-diff', before, before)['status'] == 'identical'
    assert run('geometry', virtual, '--payload-root', root / 'payloads') == geometry
    assert run('mesh-diff', before, virtual, '--after-payload-root', root / 'payloads')['status'] == 'identical'
    run('export-mesh', before, '--out', root / 'source.obj')
    assert sum(line.startswith('v ') for line in (root / 'source.obj').read_text().splitlines()) == 3
    assert run('geometry', before, '--out', before, code=2)['error'] == 'output-path'
    for path, digest in hashes.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
print(f'{checks} source morph CLI checks passed')
