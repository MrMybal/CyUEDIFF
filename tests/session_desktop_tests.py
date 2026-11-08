"""Native session restart, opt-in, precedence, corruption and alias protections in an isolated profile."""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tempfile

exe, generator = (str(Path(x).resolve()) for x in sys.argv[1:3])
git_exe = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else None
checks = 0
env = {k.upper(): v for k, v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-session-desktop-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    env['LOCALAPPDATA'] = str(root / 'unused-profile')
    fixtures = root / 'fixtures'
    subprocess.run([generator, '--write-fixtures', str(fixtures)], check=True, env=env, stdout=subprocess.DEVNULL)
    original_assets = {p: p.read_bytes() for p in fixtures.glob('*.uasset')}
    report, session, prefs = (root / name for name in ('report.json', 'session-français.json', 'preferences.json'))

    def run(*args, code=0):
        global checks
        report.unlink(missing_ok=True)
        p = subprocess.run([exe, '--smoke', '--no-previews', '--report', str(report), *map(str, args)],
                           env=env, capture_output=True, timeout=60)
        assert p.returncode == code, (args, p.returncode, p.stderr.decode('utf-8', 'replace'))
        checks += 1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None

    a, b, rig = (fixtures / name for name in ('socket-animation-0-1013.uasset',
                'socket-animation-1-1013.uasset', 'socket-rig-0-1013.uasset'))
    base = ['--before', a, '--after', b, '--pose-before-skeleton', rig,
            '--pose-after-skeleton', rig, '--pose-frame', 1, '--view', 'animations']
    controls = ['--session', session, '--restore-session']
    state = {'asset': {'animationPosePreview': True, 'animationSocketsPreview': True,
                      'animationSockets': {'selected': 'Rig.Grip', 'filter': 'Grip', 'positionTolerance': .5,
                                           'camera': {'target': [20, 20, 20], 'distance': 80, 'yaw': .3, 'pitch': .4}}}}
    d = run('--no-preferences')
    assert not d['session']['enabled'] and not d['session']['path'] and not (root / 'unused-profile').exists()
    d = run(*base, *controls, '--save-session', '--preferences', prefs, '--save-preferences',
            '--document-action', 'view-state:' + json.dumps(state), '--document-action', 'duplicate')
    assert d['session']['saved'] and d['preferences']['saved'] and not d['session']['restored']
    saved = json.loads(session.read_text(encoding='utf-8'))
    assert saved['schema'] == 'cyuediff.workspace/10' and len(saved['documents']) == 2 and saved['active'] == 1
    assert json.loads(prefs.read_text(encoding='utf-8'))['restoreSession']
    retained = d['workspace']['documents'][1]['viewState']['asset']['animationSockets']
    before = session.read_bytes()
    d = run('--preferences', prefs, '--session', session)
    assert d['session']['restored'] and not d['session']['saved'] and len(d['workspace']['documents']) == 2
    assert d['workspace']['documents'][1]['viewState']['asset']['animationSockets'] == retained
    assert d['document']['animationPose']['socketOrigins']['comparison']['changedSockets'] == 2
    assert session.read_bytes() == before
    d = run('--preferences', prefs, '--session', session, '--no-restore-session')
    assert not d['session']['restored'] and d['document'] is None and session.read_bytes() == before
    d = run('--preferences', prefs, '--session', session, '--before', rig)
    assert not d['session']['restored'] and len(d['workspace']['documents']) == 1 and d['document']
    workspace = root / 'workspace.json'
    explicit = dict(saved, documents=saved['documents'][:1], active=0)
    workspace.write_text(json.dumps(explicit), encoding='utf-8')
    d = run('--preferences', prefs, '--session', session, '--workspace', workspace)
    assert not d['session']['restored'] and len(d['workspace']['documents']) == 1 and d['workspaceFile']['loaded']
    # Smoke mode never derives an automatic session or reads the normal profile.
    d = run('--preferences', prefs)
    assert d['session']['enabled'] and not d['session']['path'] and d['document'] is None
    d = run('--preferences', prefs, *controls, '--save-session', '--save-preferences',
            '--document-action', 'session-disable')
    assert not d['session']['saved'] and not d['session']['enabled'] and session.read_bytes() == before
    assert not json.loads(prefs.read_text(encoding='utf-8'))['restoreSession']
    d = run('--preferences', prefs, '--session', session, '--save-session', '--save-preferences',
            '--document-action', 'session-enable')
    assert d['session']['saved'] and d['session']['enabled'] and d['document'] is None
    assert json.loads(prefs.read_text(encoding='utf-8'))['restoreSession']
    session.write_bytes(before)
    # Malformed/future/duplicate/oversized sessions fall back to a usable empty document and preserve bytes.
    for text in ('{', '{"schema":"future"}', '{"schema":"cyuediff.workspace/4","schema":"future"}',
                 ' ' * (1024 * 1024 + 1)):
        session.write_text(text, encoding='utf-8')
        d = run('--no-preferences', *controls, '--save-session')
        assert d['status'] == 'ready' and d['session']['diagnostic'] and not d['session']['saved']
        assert d['document'] is None and session.read_text(encoding='utf-8') == text
    session.write_bytes(before)
    alias = root / 'session-alias.json'
    os.link(session, alias)
    run('--no-preferences', '--session', session, '--restore-session', '--save-workspace', alias, code=2)
    assert session.read_bytes() == before
    alias.unlink()
    run('--no-preferences', '--session', session, '--restore-session', '--save-session', '--workspace', session, code=2)
    run('--preferences', session, '--session', session, code=2)
    run('--no-preferences', '--session', root / 'wrong.uasset', code=2)
    run('--no-preferences', '--save-session', code=2)
    run('--no-preferences', '--restore-session', code=2)
    run('--no-preferences', '--new-document', '--session', session, code=2)
    assert session.read_bytes() == before
    # All data-only source contexts survive; the no-Git desktop leaves a Git session untouched.
    historical = json.loads(before)
    historical['documents'] = [historical['documents'][0]]
    historical['active'] = 0
    settings = historical['documents'][0]['settings']
    settings.update(source=1, repository=str(root / 'repo'), before='Content/Clip.uasset', after='',
                    pose_before_skeleton='Content/Rig.uasset', pose_after_skeleton='Content/Rig.uasset',
                    project='', after_project='', engine='', after_engine='')
    if git_exe:
        repo = root / 'repo'
        (repo / 'Content').mkdir(parents=True)
        env.update(GIT_CONFIG_COUNT='1', GIT_CONFIG_KEY_0='safe.directory', GIT_CONFIG_VALUE_0=str(repo).replace('\\', '/'))
        def git(*args):
            return subprocess.check_output([git_exe, '-C', str(repo), *args], env=env).decode().strip()
        git('init', '--quiet')
        for k, v in [('user.name', 'CyUEDiff tests'), ('user.email', 'tests@invalid.example'),
                     ('core.autocrlf', 'false'), ('core.hooksPath', 'nonexistent-hooks'), ('commit.gpgSign', 'false')]:
            git('config', k, v)
        shutil.copyfile(a, repo / 'Content/Clip.uasset')
        shutil.copyfile(rig, repo / 'Content/Rig.uasset')
        git('add', 'Content'); git('commit', '--quiet', '-m', 'first')
        settings['before_revision'] = git('rev-parse', 'HEAD')
        shutil.copyfile(b, repo / 'Content/Clip.uasset')
        git('add', 'Content'); git('commit', '--quiet', '-m', 'second')
        settings['after_revision'] = git('rev-parse', 'HEAD')
        originals = {p: p.read_bytes() for p in (repo / 'Content/Clip.uasset', repo / 'Content/Rig.uasset', repo / '.git/index')}
        status = git('status', '--porcelain=v1')
    session.write_text(json.dumps(historical), encoding='utf-8')
    before_git = session.read_bytes()
    d = run('--no-preferences', *controls)
    if git_exe:
        assert d['session']['restored'] and d['document']['animationPose']['socketOrigins']['complete']
        assert d['document']['animationPose']['socketOrigins']['comparison']['changedSockets'] == 2
        assert all(p.read_bytes() == data for p, data in originals.items()) and git('status', '--porcelain=v1') == status
    else:
        assert not d['session']['restored'] and d['session']['diagnostic'] and d['document'] is None
    assert session.read_bytes() == before_git
    # A stored cooked context without a selected container remains data-only and usable.
    cooked = json.loads(before)
    cooked['documents'] = [cooked['documents'][0]]; cooked['active'] = 0
    settings = cooked['documents'][0]['settings']
    settings.update(source=2, before='', after='', container_path='', cooked_profile=2,
                    container_schema='', pose_enabled=False)
    session.write_text(json.dumps(cooked), encoding='utf-8')
    d = run('--no-preferences', *controls)
    restored = d['workspaceFile']['context']['documents'][0]['settings']
    assert d['session']['restored'] and restored['source'] == 2 and restored['cooked_profile'] == 2
    assert restored['container_schema'] == ''
    assert all(p.read_bytes() == data for p, data in original_assets.items())
    assert not (root / 'unused-profile').exists() and not list(root.glob('*.tmp-*'))
print(f'{checks} native session restart checks passed')

