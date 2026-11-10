"""Native scalar curve review, independent contexts, workspace migration and protected dependencies."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
exe, fixture = [str(Path(p).resolve()) for p in sys.argv[1:3]]
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-curves-desktop-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=30)
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--container', root/'only-0.utoc', '--container-entry', entry,
              '--cooked-profile', 'ue5.3', '--animation-profile', 'ue5.3-windows',
              '--script-objects', root/'global.utoc', '--schema', root/'context.json',
              '--curve-settings-container', root/'settings-0.utoc', '--curve-settings-entry', entry,
              '--container-tab', 'animations']
    after = ['--after-container', root/'only-1.utoc', '--after-container-entry', entry,
             '--after-cooked-profile', 'ue5.3', '--after-animation-profile', 'ue5.3-windows',
             '--after-script-objects', root/'global.utoc', '--after-schema', root/'after-context.json',
             '--after-curve-settings-container', root/'settings-1.utoc', '--after-curve-settings-entry', entry]
    state = {'container': {'animation': {'scalarMode': True, 'curves': {
        'selected': 'name:CurveOne', 'time': .25, 'afterTime': .75, 'independentTimes': True,
        'valueTolerance': 0., 'rangeStart': -.25, 'rangeEnd': 1.25, 'beforeKey': 1,
        'properties': {'filter': 'value', 'selected': '/scalarSample/value'}}}}}
    action = ['--document-action', 'view-state:'+json.dumps(state, separators=(',', ':'))]
    def run(*args, context=common, code=0):
        global checks
        target = root/'report.json'
        target.unlink(missing_ok=True)
        p = subprocess.run([exe, '--smoke', '--no-preferences', '--no-restore-session', '--report', str(target),
                            *map(str, context), *map(str, args)], env=env, capture_output=True, timeout=45)
        assert p.returncode == code, (p.returncode, p.stderr.decode('utf-8', 'replace'))
        checks += 1
        return json.loads(target.read_text(encoding='utf-8')) if target.exists() else None
    report = run(*action)
    v = report['containerView']['animationView']['curves']
    assert v['current'] and v['before']['scalarSample']['value'] == 2. and v['plotPoints'] == [257, 0]
    assert v['before']['selectedStoredKey']['value']['value'] == 5.
    assert report['document']['container']['animations'][0]['boneChannelsDecoded'] is False
    checks += 3
    workspace = root/'workspace.json'
    report = run(*after, *action, '--locale', 'fr', '--save-workspace', workspace)
    v = report['containerView']['animationView']['curves']
    assert v['current'] and v['comparison']['complete'] and v['comparison']['encodingChanges'] == 1 and v['comparison']['valueChanges'] == 1
    assert v['before']['scalarSample']['value'] == 2. and v['after']['scalarSample']['value'] == 4.
    assert v['beforeCoverage']['settings']['schemaContextSha256'] != v['afterCoverage']['settings']['schemaContextSha256']
    saved = json.loads(workspace.read_text(encoding='utf-8'))
    assert saved['schema'] == 'cyuediff.workspace/15' and saved['documents'][0]['settings']['after_curve_settings_entry'] == entry
    assert saved['documents'][0]['viewState']['container']['animation']['scalarMode']
    checks += 5
    restored = run('--workspace', workspace, context=[])
    assert restored['containerView']['animationView']['curves']['metrics'] == v['metrics']
    assert restored['workspace']['documents'][0]['viewState']['container']['animation']['curves'] == saved['documents'][0]['viewState']['container']['animation']['curves']
    checks += 2
    report = run(*after, *action, '--after-curve-settings-entry', '', '--after-curve-settings-container', '')
    v = report['containerView']['animationView']['curves']
    assert v['comparison']['status'] == 'incomplete' and v['comparison']['encodingChanges'] == 0 and v['comparison']['valueChanges'] == 0
    assert all(c['encodingStatus'] == c['valueStatus'] == 'unavailable' for c in v['curves'])
    checks += 2
    run(*after, *action, '--after-container', root/'missing.utoc', code=2)
    legacy = json.loads(workspace.read_text(encoding='utf-8'))
    legacy['schema'] = 'cyuediff.workspace/10'
    for d in legacy['documents']:
        for key in ('defaults_context','after_defaults_context','defaults_object','after_defaults_object','inherit_after_defaults_context','max_properties','max_property_bytes','after_max_properties','after_max_property_bytes','inherit_after_source_limits','physical_cloth_profile', 'after_physical_cloth_profile', 'nanite_profile','after_nanite_profile','nanite_attributes','after_nanite_attributes','curve_settings_container','after_curve_settings_container','curve_settings_entry','after_curve_settings_entry'):
            d['settings'].pop(key)
    old = root/'version-ten.json'
    old.write_text(json.dumps(legacy), encoding='utf-8')
    report = run('--workspace', old, context=[])
    assert report['containerView']['animationView']['curves']['comparison']['status'] == 'incomplete'
    assert report['document']['container']['animations'][0]['curves']['status'] == 'opaque'
    checks += 2
    run('--after-curve-settings-entry', entry, code=2)
    run('--curve-settings-entry', 'missing', *action)
    for name in ('settings-0.utoc','settings-0.ucas','global.utoc','global.ucas','context.json','mappings.usmap'):
        alias = root/'alias.json'
        alias.unlink(missing_ok=True)
        os.link(root/name, alias)
        p = subprocess.run([exe, '--smoke', '--no-preferences', *map(str, common), '--report', str(alias)], env=env, capture_output=True, timeout=30)
        assert p.returncode == 2 and hashlib.sha256((root/name).read_bytes()).hexdigest() == protected[root/name]
        checks += 1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p,h in protected.items())
    checks += 1
print(f'{checks} cooked scalar desktop checks; protected inputs unchanged')
