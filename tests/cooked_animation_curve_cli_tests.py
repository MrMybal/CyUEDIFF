"""Whole cooked curve codec binding, independent scalar comparisons and protected dependency endpoints."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-whole-curve-cli-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=30)
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--cooked-profile', 'ue5.3', '--animation-profile', 'ue5.3-windows',
              '--script-objects', str(root/'global.utoc'), '--schema', str(root/'context.json'),
              '--curve-settings-entry', entry]
    def run(command, *args, code=0, context=common):
        global checks
        p = subprocess.run([cli, command, *map(str, args), *map(str, context)], env=env,
                           capture_output=True, text=True, encoding='utf-8', timeout=30)
        assert p.returncode == code, (command, p.returncode, p.stdout[:1500], p.stderr)
        checks += 1
        return json.loads(p.stdout) if p.stdout.strip() else p.stderr
    for codec in range(3):
        source = root/f'curves-{codec}.utoc'
        dependency = ['--curve-settings-container', root/f'settings-{codec}.utoc']
        info = run('zen-animation', source, entry, *dependency)
        assert info['curves']['status'] == 'decoded' and info['curves']['codec']['exportIndex'] == 2
        assert 'values' not in info['curves']['decoded']['curves'][0]
        checks += 2
        keys = run('zen-animation', source, entry, *dependency, '--include-keys', '1')
        assert [v['value'] for v in keys['curves']['decoded']['curves'][0]['values']] == [1., 5.]
        checks += 1
        values = run('zen-animation-curves', source, entry, *dependency, '--time', '.5')
        assert values['ready'] and values['values']['name:CurveOne']['value'] == 3.
        assert values['curveContext']['settings']['package'] == '/Game/CurveSettings'
        checks += 2
        only = run('zen-animation-curves', root/f'only-{codec}.utoc', entry, *dependency, '--time', '.25')
        assert only['ready'] and only['values']['name:CurveOne']['value'] == 2.
        checks += 1
        pair = run('zen-animation-curves-diff', source, entry, source, entry, *dependency,
                   '--after-curve-settings-container', root/f'settings-{codec}.utoc',
                   '--after-schema', root/'after-context.json', '--before-time', '0', '--after-time', '.5', code=1)
        assert pair['encodingChanges'] == 0 and pair['valueChanges'] == 1 and pair['curves']['name:CurveOne']['valueDelta'] == 2.
        assert pair['beforeCurveContext']['settings']['schemaContextSha256'] != pair['afterCurveContext']['settings']['schemaContextSha256']
        checks += 2
        same = run('zen-animation-curves-diff', source, entry, source, entry, *dependency,
                   '--after-curve-settings-container', root/f'settings-{codec}.utoc',
                   '--before-time', '0', '--after-time', '.5', '--value-tolerance', '2')
        assert same['status'] == 'identical'
        checks += 1
    a, b = root/'curves-0.utoc', root/'curves-1.utoc'
    diff = run('zen-animation-curves-diff', a, entry, b, entry,
               '--curve-settings-container', root/'settings-0.utoc', '--after-curve-settings-container', root/'settings-1.utoc', code=1)
    # Different codec encodings produce equal selected values: still a stored encoding change.

    assert diff['encodingChanges'] == 1 and diff['valueChanges'] == 0
    checks += 1
    unknown = run('zen-animation-curves', a, entry, context=common[:common.index('--curve-settings-entry')], code=2)
    assert not unknown['ready'] and unknown['declaredNames'] == ['CurveOne']
    checks += 1
    plugin = run('zen-animation-curves', a, entry, '--curve-settings-container', root/'settings-plugin.utoc', code=2)
    assert plugin['curveContext']['unavailable'] == 'unsupported-curve-codec-lookup'
    checks += 1
    pair = run('zen-animation-curves-diff', a, entry, a, entry, '--curve-settings-container', root/'settings-0.utoc',
               '--after-curve-settings-container', root/'settings-plugin.utoc', code=2)
    assert pair['encodingChanges'] == 0 and pair['valueChanges'] == 0 and pair['curves']['name:CurveOne']['valueStatus'] == 'unavailable'
    checks += 1
    run('zen-animation-curves', a, entry, '--curve-settings-container', root/'settings-0.utoc', '--time', 'nan', code=2)
    run('zen-animation-curves', a, entry, '--curve-settings-container', root/'settings-0.utoc', '--frame', '1', code=2)
    run('zen-animation-curves-diff', a, entry, a, entry, '--curve-settings-container', root/'settings-0.utoc', '--rotation-tolerance', '1', code=2)
    for name in ('settings-0.utoc', 'settings-0.ucas', 'global.utoc', 'global.ucas', 'context.json', 'mappings.usmap'):
        run('zen-animation', a, entry, '--curve-settings-container', root/'settings-0.utoc', '--out', root/name, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in protected.items())
    checks += 1
print(f'{checks} whole cooked curve CLI checks; protected inputs unchanged')
