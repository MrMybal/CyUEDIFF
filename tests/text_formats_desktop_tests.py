"""Formatted text/byte string selection and workspace restart in the desktop."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-text-desktop-', dir=Path.cwd()) as folder:
    root = Path(folder)
    env['LOCALAPPDATA'] = str(root/'unused-profile')
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    prefix = '/Script/Test.Collections.'
    report = root/'report.json'
    base = ['--container', root/'named.utoc', '--container-entry', '000000000000000000000001',
            '--cooked-profile', 'ue5.5', '--script-objects', root/'global.utoc', '--schema', root/'formats.json', '--cooked-view', 'properties']
    pair = [*base, '--after-container', root/'changed-named.utoc', '--after-cooked-profile', 'ue5.5',
            '--after-script-objects', root/'global.utoc', '--after-schema', root/'formats.json']

    def run(*args, code=0):
        global checks
        report.unlink(missing_ok=True)
        result = subprocess.run([exe, '--smoke', '--no-preferences', '--report', str(report), *map(str, args)], env=env, capture_output=True, timeout=45)
        assert result.returncode == code, (result.returncode, result.stderr.decode('utf-8', 'replace'), args)
        checks += 1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None

    first = run(*pair)
    assert first['containerView']['objectProperties']['available'] and first['containerView']['objectProperties']['comparing']; checks += 1
    for leaf, query in (('Lookup/arguments/Count/value', 'Count'), ('Lookup/arguments/Label/value/source/source', 'Label'), ('Tags', 'Tags')):
        selected = '/~1Script~1Test.Collections.'+leaf
        state = {'container': {'values': {'filter': query, 'selected': selected}}}
        workspace = root/(query+'-workspace.json')
        saved = run(*pair, '--document-action', 'view-state:'+json.dumps(state), '--save-workspace', workspace)
        assert saved['workspace']['documents'][0]['viewState']['container']['values']['selected'] == selected; checks += 1
        restored = run('--workspace', workspace, '--locale', 'fr')
        assert restored['document'] == first['document'] and restored['workspace']['documents'][0]['viewState']['container']['values']['selected'] == selected; checks += 1
    for name in ('ordered', 'transform', 'number', 'currency'):
        result = run(*pair, '--container', root/(name+'.utoc'), '--after-container', root/('changed-'+name+'.utoc'))
        assert result['containerView']['objectProperties']['available']; checks += 1
    for name, leaf in (('date', 'ticks'), ('time', 'timeZone'), ('custom-date-time', 'customPattern'), ('data', 'arguments/0/value')):
        selected = '/~1Script~1Test.Collections.Lookup/'+leaf
        state = {'container': {'values': {'filter': leaf.split('/')[-1], 'selected': selected}}}
        workspace = root/(name+'-workspace.json')
        saved = run(*pair, '--container', root/(name+'.utoc'), '--after-container', root/('changed-'+name+'.utoc'),
                    '--document-action', 'view-state:'+json.dumps(state), '--save-workspace', workspace)
        assert saved['containerView']['objectProperties']['available']; checks += 1
        restored = run('--workspace', workspace, '--locale', 'fr')
        assert restored['document'] == saved['document'] and restored['workspace']['documents'][0]['viewState']['container']['values']['selected'] == selected; checks += 1
        assert run('--before', root/(name+'.uasset'), '--after', root/('changed-'+name+'.uasset'), '--view', 'properties')['status'] == 'ready'; checks += 1
    for name in ('bad-style', 'bad-data'):
        result = run(*base, '--container', root/(name+'.utoc'))
        assert not result['containerView']['objectProperties']['available'] and result['containerView']['objectProperties']['beforeStatus'] == 'opaque'; checks += 1
    for name in ('duplicate', 'bad-argument', 'bad-rounding', 'unsupported', 'bad-utf8', 'truncated'):
        result = run(*base, '--container', root/(name+'.utoc'))
        assert not result['containerView']['objectProperties']['available'] and result['containerView']['objectProperties']['beforeStatus'] == 'opaque'; checks += 1
    assert run(*base, '--container', root/'zero.utoc')['containerView']['objectProperties']['available']; checks += 1
    for name in ('named', 'number', 'utf8'):
        result = run('--before', root/(name+'.uasset'), '--after', root/('changed-'+name+'.uasset'), '--view', 'properties', '--locale', 'fr')
        assert result['status'] == 'ready'; checks += 1
    for name in ('formats.json', 'formats.usmap', 'named.utoc', 'named.ucas'):
        run(*base, '--save-workspace', root/name, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in hashes.items()) and not (root/'unused-profile').exists(); checks += 1
print(f'{checks} text format desktop checks passed')
