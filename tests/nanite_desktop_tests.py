"""Native Nanite inspection, channel differences, independent contexts and workspace state."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, whole_fixture, attr_fixture = [str(Path(p).resolve()) for p in sys.argv[1:4]]
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-nanite-desktop-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    for fixture in (whole_fixture, attr_fixture):
        subprocess.run([fixture, '--write-fixtures', str(root)], check=True, env=env, capture_output=True, timeout=30)
    context = json.loads((root/'context.json').read_text(encoding='utf-8'))
    context['bindings']['/Script/Engine.Unused'] = {'schema': 'Collections', 'offset': 0}
    context['usmap'] = 'after.usmap'
    (root/'after.usmap').write_bytes((root/'native.usmap').read_bytes())
    (root/'after-context.json').write_text(json.dumps(context), encoding='utf-8')
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--container', root/'attrs-before.utoc', '--container-entry', entry,
              '--cooked-profile', 'ue5.3', '--mesh-profile', 'ue5.3-windows',
              '--nanite-profile', 'ue5.3-windows', '--keep-mobile-min-lod', '0',
              '--script-objects', root/'global.utoc', '--schema', root/'context.json', '--container-tab', 'nanite']
    after = ['--after-container', root/'attrs-color.utoc', '--after-container-entry', entry,
             '--after-cooked-profile', 'ue5.3', '--after-mesh-profile', 'ue5.3-windows',
             '--after-nanite-profile', 'ue5.3-windows', '--after-keep-mobile-min-lod', '0',
             '--after-script-objects', root/'global.utoc', '--after-schema', root/'after-context.json']
    state = {'container': {'nanite': {'page': 0, 'cluster': 0, 'triangle': 0, 'inspector': 2,
              'properties': {'changedOnly': False, 'filter': 'color'}, 'layout': {'columns': [1, 1.5]}}}}
    action = ['--document-action', 'view-state:'+json.dumps(state, separators=(',', ':'))]
    def run(*args, context=common, code=0):
        global checks
        report = root/'report.json'
        report.unlink(missing_ok=True)
        proc = subprocess.run([exe, '--smoke', '--no-preferences', '--no-restore-session', '--report', str(report),
                               *map(str, context), *map(str, args)], capture_output=True, env=env, timeout=45)
        assert proc.returncode == code, (proc.returncode, proc.stderr.decode('utf-8', 'replace'), args)
        checks += 1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    single = run(*action)
    v = single['containerView']['naniteView']
    assert not v['busy'] and not v['error'] and v['sceneTriangles'] == 1 and v['renderedEdges'] == 3
    assert v['details']['corners'][0]['attributes']['normalStatus'] == 'decoded'
    checks += 2
    workspace = root/'workspace.json'
    report = run(*after, *action, '--locale', 'fr', '--save-workspace', workspace)
    v = report['containerView']['naniteView']
    assert v['comparison']['complete'] and v['comparison']['modifiedAttributeTriangles'] == 1
    assert v['comparison']['surfaceStatus'] == 'identical' and v['paired'] and v['selectedVisible']
    assert any(a['attributes']['color'] != b['attributes']['color'] for a, b in zip(v['details']['corners'], v['peerDetails']['corners']))
    assert v['coverage'][0]['source']['schemaContextSha256'] != v['coverage'][1]['source']['schemaContextSha256']
    saved = json.loads(workspace.read_text(encoding='utf-8'))
    d = saved['documents'][0]
    assert saved['schema'] == 'cyuediff.workspace/14' and d['containerTab'] == 10
    assert d['settings']['nanite_profile'] == d['settings']['after_nanite_profile'] == 1
    assert d['settings']['nanite_attributes'] and d['settings']['after_nanite_attributes']
    assert d['viewState']['container']['beforeNanite'] == v['beforeObject']
    assert d['viewState']['container']['afterNanite'] == v['afterObject']
    checks += 9
    restored = run('--workspace', workspace, context=[])
    rv = restored['containerView']['naniteView']
    assert rv['comparison'] == v['comparison'] and rv['state'] == d['viewState']['container']['nanite']
    checks += 1
    unknown = run(*after, '--after-container', root/'attrs-before.utoc', '--after-nanite-attributes', '0', *action)
    uv = unknown['containerView']['naniteView']
    assert not uv['comparison']['complete'] and uv['comparison']['modifiedAttributeTriangles'] == 0
    assert uv['paired'] and uv['inspectorRows']['added'] == uv['inspectorRows']['removed'] == uv['inspectorRows']['modified'] == 0
    checks += 2
    removed = run(*after, '--after-container', root/'empty.utoc', *action)['containerView']['naniteView']
    assert removed['comparison']['complete'] and removed['comparison']['removedTriangles'] == 1
    assert removed['paired'] and removed['peerDetails'] is None
    checks += 2
    missing = run(*after, '--after-container', root/'missing.utoc', *action)['containerView']['naniteView']
    assert not missing['comparison']['complete'] and missing['comparison']['removedTriangles'] == 0
    checks += 1
    # Legacy workspaces must opt into no native decoder and must not infer a producer profile.
    old = json.loads(workspace.read_text(encoding='utf-8'))
    old['schema'] = 'cyuediff.workspace/11'
    for doc in old['documents']:
        for key in ('max_properties','max_property_bytes','after_max_properties','after_max_property_bytes','inherit_after_source_limits','physical_cloth_profile', 'after_physical_cloth_profile', 'nanite_profile', 'after_nanite_profile', 'nanite_attributes', 'after_nanite_attributes'):
            doc['settings'].pop(key)
        for key in ('nanite', 'beforeNanite', 'afterNanite'):
            doc['viewState']['container'].pop(key)
        doc['containerTab'] = 6
    legacy = root/'legacy.json'
    legacy.write_text(json.dumps(old), encoding='utf-8')
    migrated = run('--workspace', legacy, context=[])
    assert migrated['containerView']['naniteView'] is None
    settings = migrated['workspaceFile']['context']['documents'][0]['settings']
    assert settings['nanite_profile'] == settings['after_nanite_profile'] == 0
    checks += 2
    run('--after-nanite-profile', 'ue5.3-windows', code=2)
    run('--nanite-profile', 'ue5.5', code=2)
    run('--nanite-attributes', '2', code=2)
    for name in ('attrs-before.utoc', 'attrs-before.ucas', 'attrs-color.utoc', 'attrs-color.ucas',
                 'context.json', 'native.usmap', 'after-context.json', 'after.usmap', 'global.utoc', 'global.ucas'):
        alias = root/'alias.json'
        alias.unlink(missing_ok=True)
        os.link(root/name, alias)
        proc = subprocess.run([exe, '--smoke', '--no-preferences', *map(str, common), *map(str, after), '--report', str(alias)],
                              capture_output=True, env=env, timeout=30)
        assert proc.returncode == 2 and hashlib.sha256((root/name).read_bytes()).hexdigest() == protected[root/name]
        checks += 1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in protected.items())
    checks += 1
print(f'{checks} Nanite desktop checks; independent protected inputs unchanged')
