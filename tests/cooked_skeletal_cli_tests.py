"""Original inline skeletal fixtures: skinning, diffs, context and protected inputs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:3]]
desktop = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else None
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-skeletal-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], check=True, env=env, capture_output=True, timeout=20)
    originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    common = ['--cooked-profile', 'ue5.3', '--script-objects', str(root/'global.utoc'), '--schema', str(root/'context.json')]
    native = ['--skeletal-profile', 'ue5.3-windows', '--keep-mobile-min-lod', '0', '--has-vertex-colors', '0']
    entry = '000000000000000000000001'
    def run(command, *arguments, code=0, profile=True):
        global checks
        p = subprocess.run([cli, command, *common, *(native if profile else []), *map(str, arguments)], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, p.stderr.decode('utf-8', 'replace')
        checks += 1
        return json.loads(p.stdout) if code != 2 and p.stdout else p.stderr.decode('utf-8', 'replace')
    inline, changed, corrupt = [root/(name+'.utoc') for name in ('inline', 'changed', 'corrupt')]
    decoded = run('zen-skeletal', inline, entry)
    g = decoded['lods'][0]['geometry']
    assert len(g['vertices']) == 3 and len(g['skin']['bones']) == 2
    assert g['skin']['vertices']['0']['default'] == {'PF_DXT1': 65535}
    assert decoded['tail']['decoded'] is False and decoded['tail']['bytes'] == 1
    checks += 3
    assert run('zen-skeletal-diff', inline, entry, inline, entry)['status'] == 'identical'
    diff = run('zen-skeletal-diff', inline, entry, changed, entry, code=1)
    assert diff['status'] == 'changed' and diff['beforeMesh']['packageSha256'] != diff['afterMesh']['packageSha256']
    assert diff['beforeMesh']['schemaContextSha256'] == diff['afterMesh']['schemaContextSha256']
    checks += 3
    for command, args in [('zen-skeletal', [inline, entry]), ('zen-skeletal-diff', [inline, entry, inline, entry])]:
        assert 'cooked-skeletal-profile' in run(command, *args, profile=False, code=2)
    assert 'container-integrity' in run('zen-skeletal', corrupt, entry, code=2)
    run('zen-skeletal', inline, entry, '--skeletal-profile', 'bad', code=2)
    run('zen-skeletal', inline, entry, '--has-vertex-colors', '2', code=2)
    run('zen-skeletal', inline, entry, '--after-has-vertex-colors', '0', code=2)
    run('zen-skeletal-diff', inline, entry, changed, entry, '--after-export-index', '2', code=2)
    run('zen-skeletal', inline, entry, '--out', inline.with_suffix('.ucas'), code=2)
    run('zen-skeletal', inline, entry, '--out', root/'native.usmap', code=2)
    run('zen-skeletal', inline, entry, '--out', root/'global.ucas', code=2)
    streamed = root/'streamed.utoc'
    decoded_stream = run('zen-skeletal', streamed, entry)
    assert len(decoded_stream['lods']) == 2 and decoded_stream['lods'][0]['bulk']['storage'] == 'external'
    assert decoded_stream['lods'][0]['geometry'] == decoded_stream['lods'][1]['geometry']
    assert run('zen-skeletal-diff', root/'stream-inline.utoc', entry, streamed, entry)['status'] == 'identical'
    external_diff = run('zen-skeletal-diff', streamed, entry, root/'external-changed.utoc', entry, code=1)
    assert external_diff['beforeMesh']['packageSha256'] == external_diff['afterMesh']['packageSha256']
    assert external_diff['beforeMesh']['lods'][0]['bulk']['sha256'] != external_diff['afterMesh']['lods'][0]['bulk']['sha256']
    missing = run('zen-skeletal', root/'missing.utoc', entry)
    assert missing['lods'][0]['unavailable'] == 'missing-chunk' and 'geometry' not in missing['lods'][0]
    assert 'cooked-skeletal-unavailable' in run('zen-skeletal-diff', streamed, entry, root/'missing.utoc', entry, code=2)
    assert 'container-integrity' in run('zen-skeletal', root/'outside-corrupt.utoc', entry, code=2)
    checks += 7
    if desktop:
        before_native = ['--skeletal-profile', 'ue5.3-windows', '--skeletal-keep-mobile-min-lod', '0', '--has-vertex-colors', '0']
        after_native = ['--after-skeletal-profile', 'ue5.3-windows', '--after-skeletal-keep-mobile-min-lod', '0', '--after-has-vertex-colors', '0']
        after_context = ['--after-cooked-profile', 'ue5.3', '--after-script-objects', str(root/'global.utoc'), '--after-schema', str(root/'context.json')]
        def render(*arguments):
            p = subprocess.run([desktop, '--smoke', '--no-preferences', *map(str, arguments)], env=env, capture_output=True, timeout=45)
            assert p.returncode == 0, p.stderr.decode('utf-8', 'replace')
        for lang in ('en', 'fr'):
            report = root/(lang+'.json')
            workspace = root/(lang+'-workspace.json')
            render('--locale', lang, '--container', inline, '--container-entry', entry,
                   '--after-container', root/'skin-changed.utoc', '--container-tab', 'meshes',
                   '--report', report, '--save-workspace', workspace, *common, *before_native, *after_native, *after_context)
            data = json.loads(report.read_text(encoding='utf-8'))
            doc = data['document']['container']
            assert doc['meshPreviews'] == doc['after']['meshPreviews'] == 1
            assert doc['meshDifferences'][0]['comparison']['status'] == 'changed'
            views = data['containerView']['meshViews']
            assert len(views) == 2 and all(v['comparisonKnown'] and v['skinAvailable'] and v['skin']['modified'] == 1 for v in views)
            assert sum(v['changes']['modified'] for v in views) > 0
            saved = json.loads(workspace.read_text(encoding='utf-8'))
            assert saved['schema'] == 'cyuediff.workspace/14'
            settings = saved['documents'][0]['settings']
            assert settings['skeletal_profile'] == settings['after_skeletal_profile'] == 1
            assert settings['skeletal_mobile_lod'] == settings['after_skeletal_mobile_lod'] == 1
            assert settings['skeletal_colors'] == settings['after_skeletal_colors'] == 1
            state = saved['documents'][0]['viewState']['container']
            state['meshSkin'] = True
            state['meshSkinView']['selected'] = 'vertex/1'
            state['meshSkinView']['properties'] = {'filter': '', 'changedOnly': False, 'selected': None}
            workspace.write_text(json.dumps(saved), encoding='utf-8')
            reopened = root/(lang+'-reopened.json')
            render('--workspace', workspace, '--report', reopened, '--save-workspace', root/(lang+'-resaved.json'))
            restored = json.loads((root/(lang+'-resaved.json')).read_text(encoding='utf-8'))
            state = restored['documents'][0]['viewState']['container']
            assert state['meshSkin'] and state['meshSkinView']['selected'] == 'vertex/1'
            assert json.loads(reopened.read_text(encoding='utf-8'))['containerView']['tab'] == 6
            checks += 9
        unknown = root/'unknown.json'
        render('--container', inline, '--container-entry', entry, '--after-container', inline,
               '--container-tab', 'meshes', '--report', unknown, *common, *before_native, *after_context)
        views = json.loads(unknown.read_text(encoding='utf-8'))['containerView']['meshViews']
        assert len(views) == 1 and not views[0]['comparisonKnown'] and not views[0]['skin']['comparing']
        assert views[0]['changes']['removed'] == views[0]['changes']['modified'] == 0
        checks += 2
        external_report = root/'external-ui.json'
        render('--container', streamed, '--container-entry', entry, '--after-container', root/'external-changed.utoc',
               '--container-tab', 'meshes', '--report', external_report, *common, *before_native, *after_native, *after_context)
        doc = json.loads(external_report.read_text(encoding='utf-8'))['document']['container']
        assert doc['meshPreviews'] == doc['after']['meshPreviews'] == 2
        assert doc['meshDifferences'][0]['comparison']['status'] == 'changed'
        assert doc['meshDifferences'][1]['comparison']['status'] == 'identical'
        missing_report = root/'missing-ui.json'
        render('--container', streamed, '--container-entry', entry, '--after-container', root/'missing.utoc',
               '--container-tab', 'meshes', '--report', missing_report, *common, *before_native, *after_native, *after_context)
        views = json.loads(missing_report.read_text(encoding='utf-8'))['containerView']['meshViews']
        unknown_lod = next(v for v in views if v['key'].endswith('LOD 0'))
        assert not unknown_lod['comparisonKnown'] and unknown_lod['changes']['removed'] == unknown_lod['changes']['modified'] == 0
        assert not unknown_lod['skin']['comparing']
        checks += 5
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in originals.items())
    checks += 1
print(f'{checks} cooked skeletal CLI checks passed')
