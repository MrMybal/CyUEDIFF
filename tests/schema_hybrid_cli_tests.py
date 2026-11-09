"""Exercise certified hybrid property/native structs through CLI and desktop boundaries."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture = (str(Path(p).resolve()) for p in sys.argv[1:3])
desktop = len(sys.argv) == 4 and sys.argv[3] == 'desktop'
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-hybrid-cli-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    env['LOCALAPPDATA'] = str(root/'unused-profile')
    subprocess.run([fixture, '--emit-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=30)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    entry = '000000000000000000000001'
    key = '/Script/Test.Collections.Lookup'
    report = root/'report.json'
    workspace = root/'workspace.json'
    def run(*args, code=0):
        global checks
        command = [exe]
        if desktop:
            report.unlink(missing_ok=True)
            command += ['--smoke', '--no-preferences', '--no-restore-session', '--no-previews', '--report', str(report)]
        result = subprocess.run([*command, *map(str, args)], env=env, capture_output=True, timeout=60)
        assert result.returncode == code, (result.returncode, args, result.stderr.decode('utf-8', 'replace'))
        checks += 1
        if desktop:
            return json.loads(report.read_text(encoding='utf-8')) if report.is_file() else None
        return json.loads(result.stderr if code == 2 else result.stdout)
    for profile, suffix in [('ue5.3', ''), ('ue5.5', '55')]:
        before = root/('before'+suffix+'.utoc')
        after = root/('after'+suffix+'.utoc')
        context = root/('hybrid-'+profile+'.json')
        missing = root/('unbound-'+profile+'.json')
        if desktop:
            common = ['--container', before, '--container-entry', entry, '--cooked-profile', profile,
                      '--script-objects', root/'global.utoc', '--schema', context, '--cooked-view', 'properties']
            paired = run(*common, '--after-container', after, '--after-container-entry', entry,
                         '--after-cooked-profile', profile, '--after-script-objects', root/'global.utoc',
                         '--after-schema', context, '--locale', 'en', '--save-workspace', workspace)
            assert paired['containerView']['objectProperties']['available']
            assert paired['containerView']['objectProperties']['displayTruncated'] is False
            value = paired['document']['container']['zen']['exports'][0]['propertyRead']['properties'][key]
            assert value['structTailLayout'] == 'Tethers' and value['value']['native']['batches'][0][0]['length'] == 2.5
            diff = paired['document']['container']['selectedComparison']['zen']
            assert any(c['path'].endswith('/value/native/batches/0/0/length') for c in diff['changes'])
            restored = run('--workspace', workspace, '--locale', 'fr')
            assert restored['document'] == paired['document'] and restored['locale'] == 'fr'
            unbound = list(common)
            unbound[unbound.index('--schema') + 1] = missing
            bad = run(*unbound)
            assert not bad['containerView']['objectProperties']['available'] or bad['document']['container']['zen']['propertyExportsFailed'] == 1
            selected = paired['document']['container']['zen']['exports'][0]['path']
            unknown = run(*common, '--after-container', after, '--after-container-entry', entry,
                          '--after-cooked-profile', profile, '--after-script-objects', root/'global.utoc',
                          '--after-schema', missing, '--document-action',
                          'view-state:'+json.dumps({'container':{'object':selected}}))
            status = unknown['containerView']['objectProperties']
            assert status['available'] and not status['comparing'] and status['beforeStatus']=='decoded' and status['afterStatus']=='opaque'
            run(*common, '--save-workspace', context, code=2)
        else:
            common = ['--cooked-profile', profile, '--script-objects', root/'global.utoc', '--schema', context]
            inspected = run('zen-inspect', before, entry, *common)
            value = inspected['exports'][0]['propertyRead']['properties'][key]
            assert inspected['propertyExportsDecoded'] == 2 and value['structTailLayout'] == 'Tethers'
            assert value['value']['properties']['/Script/Test.Record.Code']['value'] == 17
            assert value['value']['native']['batches'][0][0]['length'] == 2.5
            diff = run('zen-diff', before, entry, after, entry, *common, code=1)
            assert not diff['contextChanged'] and any(c['path'].endswith('/value/native/batches/0/0/length') for c in diff['changes'])
            for container, schema in [(before, missing), (root/('truncated'+suffix+'.utoc'), context)]:
                failed = run('zen-inspect', container, entry, '--cooked-profile', profile,
                             '--script-objects', root/'global.utoc', '--schema', schema)
                prop = failed['exports'][0]['propertyRead']
                assert failed['propertyExportsFailed'] == 1 and prop['status'] == 'opaque' and 'properties' not in prop
            run('zen-inspect', before, entry, *common, '--out', context, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == value for p, value in hashes.items())
    assert not (root/'unused-profile').exists()
    checks += 2
print(str(checks)+' hybrid schema '+('desktop' if desktop else 'CLI')+' checks passed')
