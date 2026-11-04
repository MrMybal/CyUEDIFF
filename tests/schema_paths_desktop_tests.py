"""Typed optional/field path previews and selections across workspace restart."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
env = {k.upper(): v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-paths-desktop-', dir=Path.cwd()) as folder:
    root = Path(folder)
    env['LOCALAPPDATA'] = str(root / 'unused-profile')
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    prefix = '/Script/Test.Collections.'
    report = root / 'report.json'
    base = ['--container', root / 'mapped.utoc', '--container-entry', '000000000000000000000001',
            '--cooked-profile', 'ue5.5', '--script-objects', root / 'global.utoc', '--schema', root / 'paths.json', '--cooked-view', 'properties']
    pair = [*base, '--after-container', root / 'changed.utoc', '--after-cooked-profile', 'ue5.5',
            '--after-script-objects', root / 'global.utoc', '--after-schema', root / 'paths.json']

    def run(*args, code=0):
        global checks
        report.unlink(missing_ok=True)
        result = subprocess.run([exe, '--smoke', '--no-preferences', '--report', str(report), *map(str, args)], env=env, capture_output=True, timeout=45)
        assert result.returncode == code, (result.returncode, result.stderr.decode('utf-8', 'replace'), args)
        checks += 1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None

    first = run(*pair)
    view = first['containerView']['objectProperties']
    assert view['available'] and view['comparing']; checks += 1
    changes = first['document']['container']['selectedComparison']['zen']['changes']
    assert all(any(c['path'].endswith(s) for c in changes) for s in ('/value/value/path/0', '/value/value/owner/path', '/value/value')); checks += 1
    for leaf, query in (('Lookup/value/path/0', 'path'), ('After/value', 'After')):
        selected = '/~1Script~1Test.Collections.' + leaf
        state = {'container': {'values': {'filter': query, 'selected': selected}}}
        workspace = root / (query + '-workspace.json')
        saved = run(*pair, '--document-action', 'view-state:' + json.dumps(state), '--save-workspace', workspace)
        assert saved['workspace']['documents'][0]['viewState']['container']['values']['selected'] == selected; checks += 1
        restored = run('--workspace', workspace, '--locale', 'fr')
        assert restored['document'] == first['document'] and restored['workspace']['documents'][0]['viewState']['container']['values']['selected'] == selected; checks += 1
    unset = run(*pair, '--after-container', root / 'unset.utoc')
    assert any(c['path'].endswith('/value/isSet') and c['after'] is False for c in unset['document']['container']['selectedComparison']['zen']['changes']); checks += 1
    null = run(*base, '--container', root / 'set-null.utoc')
    props = null['document']['container']['zen']['exports'][0]['propertyRead']['properties']
    assert props[prefix + 'After']['value']['isSet'] and props[prefix + 'After']['value']['value'] == 0; checks += 1
    for name in ('truncated', 'bad-marker', 'bad-name', 'bad-owner', 'old-release', 'old-fortnite'):
        data = run(*base, '--container', root / (name + '.utoc'))
        assert not data['containerView']['objectProperties']['available'] and data['containerView']['objectProperties']['beforeStatus'] == 'opaque'; checks += 1
    assert run(*base, '--container', root / 'zero.utoc')['containerView']['objectProperties']['available']; checks += 1
    older = run(*base, '--container', root / 'paths53.utoc', '--cooked-profile', 'ue5.3', '--schema', root / 'field53.json')
    assert older['document']['container']['zen']['propertyExportsDecoded'] == 2; checks += 1
    for protected in ('paths.json', 'paths.usmap', 'mapped.utoc', 'mapped.ucas'):
        run(*base, '--save-workspace', root / protected, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in hashes.items()) and not (root / 'unused-profile').exists(); checks += 1
print(f'{checks} cooked optional/field path desktop checks passed')
