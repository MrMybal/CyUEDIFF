"""Explicit schema provenance, typed cooked diffs and protected input files."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-schema-cli-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', temp], check=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    before, after = root/'schema.utoc', root/'changed.utoc'
    context = ['--cooked-profile', 'ue5.3', '--script-objects', root/'global.utoc', '--schema', root/'schema.json']

    def run(*args, code=0, error=None):
        global checks
        result = subprocess.run([cli, *map(str, args)], capture_output=True, timeout=30)
        assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
        report = json.loads(result.stderr if code == 2 and result.stderr else result.stdout)
        if error:
            assert report['error'] == error, report
        checks += 1
        return report

    assert run('usmap', root/'test.usmap')['version'] == 4
    package = run('zen-inspect', before, entry, *context)
    assert package['propertyExportsDecoded'] == 2 and package['propertiesDecoded'] is True
    prop = package['exports'][0]['propertyRead']
    assert prop['properties']['DemoAsset.Health']['value'] == 42
    assert prop['mappingVerified'] is False and prop['defaultsResolved'] is False
    snapshot = run('zen-inspect', before, entry, *context, '--snapshot')
    assert snapshot['objects']['/Game/Example.Example']['properties']['DemoAsset.Health']['value'] == 42
    catalog = run('zen-catalog', before, *context, '--snapshot')
    assert catalog['propertiesDecoded'] and catalog['propertyExportsDecoded'] == 2
    assert run('zen-diff', before, entry, before, entry, *context)['status'] == 'identical'
    diff = run('zen-diff', before, entry, after, entry, *context, code=1)
    assert any(c['path'].endswith('/properties/DemoAsset.Health/value') and c['before'] == 42 and c['after'] == 99 for c in diff['changes'])
    manifest = json.loads((root/'schema.json').read_text(encoding='utf-8'))
    modified = root/'modified.json'
    def write(value):
        modified.write_text(json.dumps(value), encoding='utf-8')
    other = dict(manifest, bindings={})
    write(other)
    diff = run('zen-diff', before, entry, before, entry, *context, '--after-schema', modified, code=1)
    assert diff['contextChanged'] and not diff['binaryChanged']
    write(dict(manifest, sha256='0'*64))
    run('zen-inspect', before, entry, *context, '--schema', modified, code=2, error='schema-context')
    write(dict(manifest, profile='ue5.5'))
    run('zen-inspect', before, entry, *context, '--schema', modified, code=2, error='schema-context')
    write(dict(manifest, surprise=True))
    run('zen-inspect', before, entry, *context, '--schema', modified, code=2, error='schema-context')
    write(dict(manifest, bindings={'/Script/Test.DemoAsset': {'schema': 'DemoAsset', 'offset': -1}}))
    run('zen-inspect', before, entry, *context, '--schema', modified, code=2, error='schema-context')
    modified.write_text('{"format":"one","format":"two"}', encoding='utf-8')
    run('zen-inspect', before, entry, *context, '--schema', modified, code=2, error='schema-context')
    write(dict(manifest, bindings={'/Script/Test.DemoAsset': {'schema': 'DemoAsset', 'offset': 99999}}))
    fallback = run('zen-inspect', before, entry, *context, '--schema', modified)
    assert fallback['propertyExportsFailed'] == 2 and not fallback['propertiesDecoded']
    assert fallback['exports'][0]['sha256'] == package['exports'][0]['sha256']
    run('container', before, '--schema', root/'schema.json', code=2, error='arguments')
    run('usmap', root/'test.usmap', '--snapshot', code=2, error='arguments')
    run('zen-catalog', before, *context, '--after-schema', root/'schema.json', code=2, error='arguments')
    for source in (root/'schema.json', root/'test.usmap'):
        run('zen-inspect', before, entry, *context, '--out', source, code=2, error='output-path')
        link = root/'link.json'
        os.link(source, link)
        try:
            run('zen-inspect', before, entry, *context, '--out', link, code=2, error='output-path')
        finally:
            link.unlink()
    run('usmap', root/'test.usmap', '--out', root/'test.usmap', code=2, error='output-path')
    unicode_schema = root/'schéma.json'
    unicode_schema.write_bytes((root/'schema.json').read_bytes())
    assert run('zen-inspect', before, entry, *context, '--schema', unicode_schema)['sha256'] == package['sha256']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == sha for p, sha in hashes.items())
print(f'{checks} schema CLI checks passed; original input hashes unchanged')
