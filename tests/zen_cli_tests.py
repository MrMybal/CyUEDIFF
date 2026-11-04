"""Cooked CLI contracts, distinct contexts and original-source output protection."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixtures = [str(Path(p).resolve()) for p in sys.argv[1:]]
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-zen-cli-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixtures, '--write-fixtures', temp], check=True, timeout=20)
    originals = list(root.iterdir())
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
    before, after, glob = root/'test.utoc', root/'changed.utoc', root/'global.utoc'
    entry = '000000000000000000000001'
    profile = ['--cooked-profile', 'ue5.3']
    context = [*profile, '--script-objects', glob]

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
    assert package['exports'][0]['class'] == '/Script/Test.DemoAsset'
    assert package['propertiesDecoded'] is False
    assert run('zen-inspect', before, entry, *profile)['exports'][0]['class'].startswith('$script:')
    snapshot = run('zen-inspect', before, entry, *context, '--snapshot')
    assert '/Game/Example.Example.Nested' in snapshot['objects']
    catalog = run('zen-catalog', before, *context, '--snapshot')
    assert catalog['complete'] and catalog['exportCount'] == 2 and catalog['packages'][0]['snapshot'] == snapshot
    assert 'snapshot' not in run('zen-catalog', before, *context)['packages'][0]
    assert run('zen-diff', before, entry, before, entry, *context)['status'] == 'identical'
    diff = run('zen-diff', before, entry, after, entry, *context, code=1)
    assert len(diff['changes']) == 1 and diff['changes'][0]['path'].endswith('/opaque/sha256')
    diff = run('zen-diff', before, entry, before, entry, *context, '--after-script-objects', root/'other-global.utoc', code=1)
    assert diff['contextChanged'] and not diff['binaryChanged'] and diff['status'] == 'changed'
    assert not run('zen-catalog', before, *context, '--max-packages', '0', code=2)['complete']
    assert run('zen-catalog', before, *context, '--max-exports', '1', code=2)['packageCount'] == 0
    for args in [('zen-inspect', before, entry), ('zen-catalog', before)]:
        run(*args, code=2, error='zen-profile')
    run('zen-inspect', before, entry, '--cooked-profile', 'ue5.4', code=2, error='zen-profile')
    run('zen-inspect', before, 'missing', *context, code=2, error='zen-entry')
    run('zen-inspect', glob, '000000000000000000000005', *context, code=2, error='zen-entry')
    run('zen-inspect', before, entry, *profile, '--script-objects', before, code=2, error='zen-entry')
    run('zen-inspect', before, entry, *context, '--project', 'ignored.uproject', code=2, error='arguments')
    run('container', before, *profile, code=2, error='arguments')
    run('zen-catalog', before, *context, '--after-cooked-profile', 'ue5.5', code=2, error='arguments')
    run('zen-diff', before, entry, before, entry, *context, '--snapshot', code=2, error='arguments')
    for source in (before, root/'test.ucas', glob, root/'global.ucas'):
        run('zen-inspect', before, entry, *context, '--out', source, code=2, error='output-path')
        link = root/'hardlink.json'
        os.link(source, link)
        try:
            run('zen-inspect', before, entry, *context, '--out', link, code=2, error='output-path')
        finally:
            link.unlink()
    unicode_path = root/'échantillon.utoc'
    unicode_path.write_bytes(before.read_bytes())
    unicode_path.with_suffix('.ucas').write_bytes((root/'test.ucas').read_bytes())
    assert run('zen-inspect', unicode_path, entry, *context)['sha256'] == package['sha256']
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == sha for p, sha in hashes.items())
print(f'{checks} cooked CLI checks passed; original source hashes unchanged')
