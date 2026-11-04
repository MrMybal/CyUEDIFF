"""Cooked collection projections, property diff paths, opaque fallback and immutable sources."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-schema-collections-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', temp], check=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    before, changed, reordered = (root/(name+'.utoc') for name in ('collections', 'changed', 'reordered'))
    context = ['--cooked-profile', 'ue5.3', '--script-objects', root/'global.utoc', '--schema', root/'collections.json']
    modern_context = ['--cooked-profile', 'ue5.5', '--script-objects', root/'global.utoc', '--schema', root/'modern.json']
    lookup, tags = '/Script/Test.Collections.Lookup', '/Script/Test.Collections.Tags'
    object_path = '/Game/Example.Example'

    def run(*args, code=0, error=None):
        global checks
        result = subprocess.run([cli, *map(str, args)], capture_output=True, timeout=30)
        assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
        report = json.loads(result.stderr if code == 2 and result.stderr else result.stdout)
        if error:
            assert report['error'] == error, report
        checks += 1
        return report

    package = run('zen-inspect', before, entry, *context)
    assert package['propertyExportsDecoded'] == 2 and package['propertyExportsFailed'] == 0
    props = package['exports'][0]['propertyRead']['properties']
    assert props[lookup]['value']['kind'] == 'stored-map-delta' and not props[lookup]['value']['replace']
    assert props[lookup]['value']['removed']['"Nested"'] == 'Nested'
    assert props[tags]['value']['removed']['99'] == 99
    assert props[tags]['value']['entries'] == {'3': 3, '5': 5}
    assert package['exports'][0]['propertyRead']['defaultsResolved'] is False
    snapshot = run('zen-inspect', before, entry, *context, '--snapshot')
    assert snapshot['objects'][object_path]['properties'] == props
    assert run('zen-diff', before, entry, before, entry, *context)['status'] == 'identical'
    diff = run('zen-diff', before, entry, changed, entry, *context, code=1)
    assert any(c['path'].endswith('/properties/~1Script~1Test.Collections.Lookup/value/entries/"None"/value') and c['before'] == 20 and c['after'] == 99 for c in diff['changes'])
    diff = run('zen-diff', before, entry, reordered, entry, *context, code=1)
    assert len(diff['changes']) == 1 and diff['changes'][0]['path'].endswith('/opaque/sha256')
    assert run('zen-inspect', reordered, entry, *context, '--snapshot')['objects'][object_path]['properties'] == props
    catalog = run('zen-catalog', before, *context, '--snapshot')
    assert catalog['complete'] and catalog['propertyExportsDecoded'] == 2 and catalog['packages'][0]['snapshot'] == snapshot
    duplicate = run('zen-inspect', root/'duplicate.utoc', entry, *context)
    assert duplicate['propertyExportsFailed'] == 1 and duplicate['exports'][0]['propertyRead']['error'] == 'schema-identity'
    assert duplicate['exports'][0]['sha256'] and duplicate['exports'][0]['propertyRead']['status'] == 'opaque'
    fallback = run('zen-inspect', root/'duplicate.utoc', entry, *context, '--snapshot')['objects'][object_path]
    assert 'properties' not in fallback and fallback['propertyRead']['status'] == 'opaque'
    partial = run('zen-catalog', root/'duplicate.utoc', *context)
    assert partial['complete'] and partial['propertyExportsFailed'] == 1
    replacement = run('zen-inspect', root/'replace.utoc', entry, *modern_context)
    assert replacement['exports'][0]['propertyRead']['properties'][lookup]['value']['replace'] is True
    assert run('zen-inspect', root/'replace-legacy.utoc', entry, *context)['exports'][0]['propertyRead']['error'] == 'schema-type'
    diff = run('zen-diff', before, entry, root/'replace.utoc', entry, *context, '--after-cooked-profile', 'ue5.5', '--after-schema', root/'modern.json', code=1)
    assert diff['contextChanged'] and any(c['path'].endswith('/value/replace') and c['after'] is True for c in diff['changes'])
    for source in (root/'collections.usmap', root/'collections.json'):
        run('zen-inspect', before, entry, *context, '--out', source, code=2, error='output-path')
        link = root/'hardlink.json'
        os.link(source, link)
        try:
            run('zen-inspect', before, entry, *context, '--out', link, code=2, error='output-path')
        finally:
            link.unlink()
    unicode_manifest = root/'schéma-collections.json'
    unicode_manifest.write_bytes((root/'collections.json').read_bytes())
    assert run('zen-inspect', before, entry, *context, '--schema', unicode_manifest)['sha256'] == package['sha256']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == sha for p, sha in hashes.items())
print(f'{checks} unversioned collection CLI checks passed; original hashes unchanged')
