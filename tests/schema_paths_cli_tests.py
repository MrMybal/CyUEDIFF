"""Mapped optionals and field paths through cooked CLI inspection and diffs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-paths-cli-', dir=Path.cwd()) as folder:
    root = Path(folder)
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    prefix = '/Script/Test.Collections.'
    common = ['--cooked-profile', 'ue5.5', '--script-objects', root / 'global.utoc', '--schema', root / 'paths.json']

    def run(*args, code=0):
        global checks
        result = subprocess.run([cli, *map(str, args)], env=env, capture_output=True, timeout=30)
        assert result.returncode == code, (result.returncode, result.stderr.decode('utf-8', 'replace'), args)
        checks += 1
        return json.loads(result.stderr if code == 2 else result.stdout)

    def inspect(name, *args):
        return run('zen-inspect', root / (name + '.utoc'), entry, *common, *args)

    before = inspect('mapped')
    props = before['exports'][0]['propertyRead']['properties']
    optional = props[prefix + 'Lookup']['value']
    assert optional['isSet'] and optional['value']['path'] == ['Nested', 'Example']; checks += 1
    assert optional['value']['owner']['path'] == '/Game/Example.Example'; checks += 1
    assert props[prefix + 'Tags']['value']['path'] == ['Example']; checks += 1
    assert props[prefix + 'After']['value']['value'] == 17; checks += 1
    changed = run('zen-diff', root / 'mapped.utoc', entry, root / 'changed.utoc', entry, *common, code=1)
    for suffix in ('/value/value/path/0', '/value/value/owner/path', '/value/path/0', '/value/value'):
        assert any(c['path'].endswith(suffix) for c in changed['changes']); checks += 1
    assert changed['binaryChanged'] and not changed['contextChanged']; checks += 1
    run('zen-diff', root / 'mapped.utoc', entry, root / 'mapped.utoc', entry, *common)
    cleared = inspect('unset')['exports'][0]['propertyRead']['properties']
    for field in ('Lookup', 'After'):
        assert cleared[prefix + field]['value'] == {'kind': 'optional', 'isSet': False}; checks += 1
    clear_diff = run('zen-diff', root / 'mapped.utoc', entry, root / 'unset.utoc', entry, *common, code=1)
    assert any(c['path'].endswith('/value/isSet') and c['after'] is False for c in clear_diff['changes']); checks += 1
    null = inspect('set-null')['exports'][0]['propertyRead']['properties']
    assert null[prefix + 'Lookup']['value']['isSet'] and null[prefix + 'Lookup']['value']['value']['owner'] is None; checks += 1
    assert null[prefix + 'After']['value'] == {'kind': 'optional', 'isSet': True, 'value': 0}; checks += 1
    zero = inspect('zero')['exports'][0]['propertyRead']['properties']
    assert all(p['serializedZero'] for p in zero.values()) and not zero[prefix + 'Lookup']['value']['isSet']; checks += 1
    for name in ('truncated', 'bad-marker', 'bad-name', 'bad-owner', 'old-release', 'old-fortnite'):
        opaque = inspect(name)['exports'][0]['propertyRead']
        assert opaque['status'] == 'opaque' and 'properties' not in opaque; checks += 1
    old_common = ['--cooked-profile', 'ue5.3', '--script-objects', root / 'global.utoc']
    paths53 = run('zen-inspect', root / 'paths53.utoc', entry, *old_common, '--schema', root / 'field53.json')
    assert paths53['propertyExportsDecoded'] == 2; checks += 1
    optional53 = run('zen-inspect', root / 'optional53.utoc', entry, *old_common, '--schema', root / 'optional53.json')
    assert optional53['exports'][0]['propertyRead']['status'] == 'opaque'; checks += 1
    snapshot = inspect('mapped', '--snapshot')
    assert snapshot['objects']['/Game/Example.Example']['properties'] == props; checks += 1
    catalog = run('zen-catalog', root / 'mapped.utoc', *common, '--snapshot')
    assert catalog['propertiesDecoded'] and catalog['propertyExportsDecoded'] == 2; checks += 1
    for protected in ('paths.json', 'paths.usmap', 'mapped.utoc', 'mapped.ucas'):
        run('zen-inspect', root / 'mapped.utoc', entry, *common, '--out', root / protected, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in hashes.items()); checks += 1
print(f'{checks} cooked optional/field path CLI checks passed')
