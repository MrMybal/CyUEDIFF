"""Original native Chaos fixture/CLI contracts; no engine data."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
cli, fixture = map(Path, sys.argv[1:3])
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0

def run(args, code=0):
    global checks
    p = subprocess.run(list(map(str, args)), capture_output=True, env=env, timeout=45)
    assert p.returncode == code, (p.returncode, p.stderr.decode('utf-8', 'replace'))
    checks += 1
    return json.loads(p.stdout) if p.stdout else None
with tempfile.TemporaryDirectory(prefix='cyuediff-physics-cli-') as directory:
    root = Path(directory)
    run([fixture, '--write-fixtures', root])
    entry = '000000000000000000000001'
    context = ['--cooked-profile', 'ue5.3', '--physics-profile', 'ue5.3-windows', '--script-objects', root / 'global.utoc', '--schema', root / 'context.json']
    a = run([cli, 'zen-physics', root / 'physics.utoc', entry, *context])
    b = run([cli, 'zen-physics', root / 'external.utoc', entry, *context])
    assert a['complete'] and len(a['bodies'][0]['formats'][0]['physics']['objects']) == 2
    assert b['bodies'][0]['formats'][0]['storage'] == 'external'
    same = run([cli, 'zen-physics-diff', root / 'physics.utoc', entry, root / 'external.utoc', entry, *context])
    assert same['status'] == 'unchanged' and (not same['changes'])
    diff = run([cli, 'zen-physics-diff', root / 'external.utoc', entry, root / 'changed.utoc', entry, *context], 1)
    assert diff['status'] == 'changed' and len(diff['changes']) == 1
    assert diff['changes'][0]['before'] == 0 and diff['changes'][0]['after'] == 0.5
    target = root / 'report.json'
    run([cli, 'zen-physics', root / 'physics.utoc', entry, *context, '--out', target])
    assert json.loads(target.read_text(encoding='utf-8')) == a
    for extra in (['--physics-profile', 'unknown'], ['--after-physics-profile', 'ue5.3-windows'], ['--lod', '0'], ['--export-index', '0'], ['--mesh-profile', 'ue5.3-windows'], ['--cooked-profile', 'ue5.5']):
        run([cli, 'zen-physics', root / 'physics.utoc', entry, *context, *extra], 2)
    run([cli, 'zen-physics', root / 'corrupt.utoc', entry, *context], 2)
    run([cli, 'zen-physics', root / 'physics.utoc', entry], 2)
    run([cli, 'zen-physics-diff', root / 'external.utoc', entry, root / 'changed.utoc', entry, *context, '--after-cooked-profile', 'ue5.5'], 2)
    for source in (root / 'physics.utoc', root / 'physics.ucas', root / 'global.utoc', root / 'global.ucas', root / 'native.usmap', root / 'context.json'):
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        run([cli, 'zen-physics', root / 'physics.utoc', entry, *context, '--out', source], 2)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
print(f'{checks} cooked physics CLI checks passed')
