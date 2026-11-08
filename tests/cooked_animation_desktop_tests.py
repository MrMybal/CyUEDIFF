"""Native cooked animation review, independent sources, workspace migration and immutable endpoints."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-animation-desktop-', dir=Path.cwd()) as temp:
    root = Path(temp)
    env['LOCALAPPDATA'] = str(root/'unused-profile')
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=30)
    entry = '000000000000000000000001'
    # An original fixture with an independently addressed second member.
    toc = bytearray((root/'changed.utoc').read_bytes())
    toc[144] = 2
    (root/'independent.utoc').write_bytes(toc)
    (root/'independent.ucas').write_bytes((root/'changed.ucas').read_bytes())
    after_entry = '020000000000000000000001'
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    common = ['--container', root/'animation.utoc', '--container-entry', entry,
              '--cooked-profile', 'ue5.3', '--animation-profile', 'ue5.3-windows',
              '--script-objects', root/'global.utoc', '--schema', root/'context.json',
              '--bone-settings-container', root/'settings.utoc', '--bone-settings-entry', entry,
              '--skeleton-container', root/'skeleton.utoc', '--skeleton-entry', entry]
    after = ['--after-container', root/'independent.utoc', '--after-container-entry', after_entry,
             '--after-cooked-profile', 'ue5.3', '--after-animation-profile', 'ue5.3-windows',
             '--after-script-objects', root/'global.utoc', '--after-schema', root/'context.json',
             '--after-bone-settings-container', root/'settings.utoc', '--after-bone-settings-entry', entry,
             '--after-skeleton-container', root/'skeleton.utoc', '--after-skeleton-entry', entry]

    def run(*args, context=common, code=0):
        global checks
        target = root/'report.json'
        target.unlink(missing_ok=True)
        p = subprocess.run([exe, '--smoke', '--no-preferences', '--no-restore-session', '--report', str(target),
                            *map(str, context), *map(str, args)], env=env, capture_output=True, timeout=45)
        assert p.returncode == code, (p.returncode, p.stderr.decode('utf-8', 'replace'))
        checks += 1
        return json.loads(target.read_text(encoding='utf-8')) if target.exists() else None

    report = run('--container-tab', 'animations')
    view = report['containerView']['animationView']
    assert report['containerView']['tab'] == 9 and view['current'] and not view['error']
    assert view['before']['currentFrame']['positions'] == [2., 0., 0.]
    assert view['beforeObject'] == '/Game/Animation.Animation' and view['comparison']['status'] == 'inspection'
    assert view['plotPoints'] == [257, 0] and view['storedPoints'] == [2, 0]
    checks += 4
    state = {'container': {'animation': {'selected': 'name:Child', 'frame': 2.5, 'afterFrame': 2.5,
             'independentFrames': True, 'component': 0, 'beforeKey': 1, 'afterKey': 1,
             'properties': {'filter': 'positions', 'selected': '/currentFrame/positions/0'}}}}
    action = ['--document-action', 'view-state:'+json.dumps(state, separators=(',', ':'))]
    workspace = root/'workspace.json'
    report = run(*after, *action, '--locale', 'fr', '--save-workspace', workspace)
    view = report['containerView']['animationView']
    assert report['locale'] == 'fr' and view['current'] and view['preparedFrame'] == 2.5
    assert view['metrics']['positionDistance'] == 2.5 and view['comparison']['changedFrames'] == 1
    assert view['before']['currentFrame']['positions'][0] == 7. and view['after']['currentFrame']['positions'][0] == 9.5
    assert report['document']['container']['selectedComparison']['explicitPair']
    assert report['document']['container']['selectedComparison']['afterEntry'] == after_entry
    assert view['before']['selectedStoredKey']['value']['value'][0] == 10.
    saved = json.loads(workspace.read_text(encoding='utf-8'))
    assert saved['schema'] == 'cyuediff.workspace/12' and saved['documents'][0]['settings']['after_container_entry'] == after_entry
    assert saved['documents'][0]['viewState']['container']['animation']['properties']['filter'] == 'positions'
    checks += 8
    restored = run('--workspace', workspace, context=[])
    assert restored['containerView']['animationView']['metrics'] == view['metrics']
    assert restored['workspace']['documents'][0]['viewState']['container']['animation'] == saved['documents'][0]['viewState']['container']['animation']
    checks += 2
    report = run(*after, '--document-action', 'view-state:'+json.dumps({'container': {'animation': {'frame': 0}}}))
    view = report['containerView']['animationView']
    assert view['tracks'][0]['frameStatus'] == 'identical' and view['tracks'][0]['storedStatus'] == 'changed'
    assert view['comparison']['status'] == 'changed' and view['comparison']['changedFrames'] == 0
    checks += 2
    numeric = common[:common.index('--skeleton-container')]
    report = run(*after, context=numeric)
    view = report['containerView']['animationView']
    assert view['comparison']['status'] == 'incomplete' and view['comparison']['changedFrames'] == 0
    assert all(not t['frameKnown'] and not t['storedKnown'] for t in view['tracks'])
    checks += 2
    report = run(*after, '--after-container', root/'missing.utoc', '--after-container-entry', entry)
    view = report['containerView']['animationView']
    assert view['comparison']['status'] == 'incomplete' and view['comparison']['changedFrames'] == 0
    assert all(t['frameStatus'] == 'unavailable' for t in view['tracks'])
    checks += 2
    report = run('--container', root/'no-interpolation.utoc', *action, '--animation-interpolation', 'step')
    assert report['containerView']['animationView']['before']['currentFrame']['positions'][0] == 2.
    checks += 1
    report = run('--container', root/'no-interpolation.utoc')
    view = report['containerView']['animationView']
    assert view['before']['currentFrame']['ready'] is False and view['plotPoints'] == [0, 0] and view['storedPoints'][0] == 2
    checks += 1
    unbound = common[:common.index('--bone-settings-container')]
    report = run(context=unbound)
    assert report['containerView']['animationView']['tracks'] == []
    assert report['document']['container']['animations'][0]['unavailable'] == 'bone-compression-settings-not-bound'
    checks += 2
    report = run('--bone-settings-entry', 'missing')
    assert report['containerView']['animationView']['beforeDiagnostics'] and not report['containerView']['animationView']['current']
    checks += 1
    run('--animation-profile', 'guess', code=2)
    run('--animation-interpolation', 'guess', code=2)
    run('--after-skeleton-entry', entry, code=2)
    run(*after, '--after-container-entry', 'missing', code=2)
    # Every explicit dependency and its data partitions are guarded before capture/report writes.
    for name in ('settings.utoc', 'settings.ucas', 'skeleton.utoc', 'skeleton.ucas', 'global.ucas', 'mappings.usmap'):
        alias = root/'alias.json'
        alias.unlink(missing_ok=True)
        os.link(root/name, alias)
        p = subprocess.run([exe, '--smoke', '--no-preferences', *map(str, common), '--report', str(alias)],
                           env=env, capture_output=True, timeout=30)
        assert p.returncode == 2 and hashlib.sha256((root/name).read_bytes()).hexdigest() == protected[root/name]
        checks += 1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in protected.items())
    checks += 1
print(f'{checks} cooked animation desktop checks; source/dependency hashes unchanged')
