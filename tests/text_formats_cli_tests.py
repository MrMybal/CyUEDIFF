"""Classic/cooked text formats, byte strings, typed diffs and fallback."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-text-cli-', dir=Path.cwd()) as folder:
    root = Path(folder)
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    prefix = '/Script/Test.Collections.'
    common = ['--cooked-profile', 'ue5.5', '--script-objects', root/'global.utoc', '--schema', root/'formats.json']

    def run(*args, code=0):
        global checks
        result = subprocess.run([cli, *map(str, args)], env=env, capture_output=True, timeout=30)
        assert result.returncode == code, (result.returncode, result.stderr.decode('utf-8', 'replace'), args)
        checks += 1
        return json.loads(result.stderr if code == 2 else result.stdout)

    def classic_properties(report):
        return next(iter(report['snapshot']['objects'].values()))['properties']

    def inspect(name):
        return run('zen-inspect', root/(name+'.utoc'), entry, *common)

    first = inspect('named')
    props = first['exports'][0]['propertyRead']['properties']
    named = props[prefix+'Lookup']['value']
    assert named['kind'] == 'named-format' and named['arguments']['Count'] == {'argumentType':'int64', 'value':41}; checks += 1
    assert named['arguments']['Label']['value']['source']['source'] == 'Original label'; checks += 1
    assert props[prefix+'Tags']['value'] == 'Caf\u00e9 \U0001f600' and props[prefix+'After']['value'] == 'Stored ASCII'; checks += 1
    for name in ('named', 'ordered', 'transform', 'number', 'percent', 'currency', 'empty-named', 'empty-ordered'):
        value = inspect(name)['exports'][0]['propertyRead']['properties'][prefix+'Lookup']['value']
        classic = classic_properties(run('inspect', root/(name+'.uasset')))
        assert classic['Formatted']['value'] == value and classic['AfterFormat']['value'] == 876; checks += 1
    changed = run('zen-diff', root/'named.utoc', entry, root/'changed-named.utoc', entry, *common, code=1)
    for suffix in ('/value/format/source', '/value/arguments/Count/value', '/value/arguments/Label/value/source/source', '/value/arguments/Label/value/transformType'):
        assert any(c['path'].endswith(suffix) for c in changed['changes']); checks += 1
    assert changed['binaryChanged'] and not changed['contextChanged']; checks += 1
    reorder = run('zen-diff', root/'named.utoc', entry, root/'reordered-named.utoc', entry, *common, code=1)
    assert reorder['binaryChanged'] and not any('/properties/' in c['path'] for c in reorder['changes']); checks += 1
    for name in ('ordered', 'transform', 'number', 'currency'):
        report = run('zen-diff', root/(name+'.utoc'), entry, root/('changed-'+name+'.utoc'), entry, *common, code=1)
        assert any('/properties/' in c['path'] for c in report['changes']); checks += 1
    currency = inspect('changed-currency')['exports'][0]['propertyRead']['properties'][prefix+'Lookup']['value']
    assert currency['currencyCode'] == 'GBP' and currency['culture'] == 'en-US' and currency['formatOptions']['roundingMode'] == 2; checks += 1
    for name in ('duplicate', 'bad-argument', 'bad-rounding', 'unsupported', 'bad-utf8', 'truncated'):
        value = inspect(name)['exports'][0]['propertyRead']
        assert value['status'] == 'opaque' and 'properties' not in value; checks += 1
    bad = classic_properties(run('inspect', root/'bad-utf8.uasset'))
    assert bad['Formatted']['opaque']['reason'] == 'string-encoding' and bad['AfterFormat']['value'] == 876; checks += 1
    for name in ('utf8', 'ascii'):
        assert 'value' in classic_properties(run('inspect', root/(name+'.uasset')))['Formatted']; checks += 1
    run('diff', root/'utf8.uasset', root/'changed-utf8.uasset', code=1)
    run('diff', root/'named.uasset', root/'changed-named.uasset', code=1)
    zero = inspect('zero')['exports'][0]['propertyRead']['properties']
    assert zero[prefix+'Lookup']['value']['kind'] == 'none' and zero[prefix+'Tags']['value'] == ''; checks += 1
    snap = run('zen-inspect', root/'named.utoc', entry, *common, '--snapshot')
    assert snap['objects']['/Game/Example.Example']['properties'] == props; checks += 1
    for name in ('formats.json', 'formats.usmap', 'named.utoc', 'named.ucas'):
        run('zen-inspect', root/'named.utoc', entry, *common, '--out', root/name, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in hashes.items()); checks += 1
print(f'{checks} text format CLI checks passed')
