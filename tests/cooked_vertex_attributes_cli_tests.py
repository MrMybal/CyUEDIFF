"""Stored vertex attributes reach CLI, explicit EN/FR desktop inspection and protected outputs."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
cli, base, fixture = [str(Path(p).resolve()) for p in sys.argv[1:4]]
desktop = str(Path(sys.argv[4]).resolve()) if len(sys.argv) > 4 else None
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-vertex-attributes-', dir=Path.cwd()) as temp:
    root = Path(temp)
    for exe in (base, fixture):
        subprocess.run([exe, '--write-fixtures', str(root)], check=True, capture_output=True, env=env)
    context = json.loads((root/'context.json').read_text(encoding='utf-8'))
    context['usmap'] = 'after.usmap'
    (root/'after.usmap').write_bytes((root/'native.usmap').read_bytes())
    (root/'after-context.json').write_text(json.dumps(context), encoding='utf-8')
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--cooked-profile', 'ue5.3', '--script-objects', root/'global.utoc', '--schema', root/'context.json', '--skeletal-profile', 'ue5.3-windows', '--keep-mobile-min-lod', '0', '--has-vertex-colors', '0']
    def run(command, *args, code=0):
        global checks
        p = subprocess.run([cli, command, *map(str, common), *map(str, args)], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, (p.returncode, p.stderr.decode('utf-8', 'replace'))
        checks += 1
        return json.loads(p.stdout) if p.stdout else None
    before, after = root/'attributes.utoc', root/'attributes-changed.utoc'
    model = run('zen-skeletal', before, entry)
    channel = model['lods'][0]['vertexAttributes']['channels'][0]
    assert channel['name'] == 'Texture' and channel['encoding'] == 'float32' and channel['values'][0] == [-1]
    assert not channel['runtimeEvaluation']
    diff = run('zen-skeletal-diff', before, entry, after, entry, '--after-schema', root/'after-context.json', code=1)
    assert diff['status'] == 'changed' and diff['changes'][0]['path'] == '/vertexAttributes/channels/Texture/vertices/0/0'
    assert run('zen-skeletal-diff', before, entry, before, entry)['status'] == 'identical'
    stream = run('zen-skeletal', root/'attributes-streamed.utoc', entry)
    assert not stream['lods'][0]['inlined'] and stream['lods'][0]['vertexAttributes']['channels'][0]['values'] == channel['values']
    assert run('zen-skeletal-diff', before, entry, root/'attributes-empty.utoc', entry, code=1)['status'] == 'changed'
    checks += 5
    if desktop:
        flags = [*common]
        flags[flags.index('--keep-mobile-min-lod')] = '--skeletal-keep-mobile-min-lod'
        flags += ['--after-cooked-profile','ue5.3','--after-script-objects',root/'global.utoc','--after-schema',root/'after-context.json','--after-skeletal-profile','ue5.3-windows','--after-skeletal-keep-mobile-min-lod','0','--after-has-vertex-colors','0']
        state = {'container': {'mesh': '/Game/Texture.Texture LOD 0', 'meshAttributes': True, 'meshAttributeView': {'channel': 'Texture', 'vertex': 0, 'changedOnly': True, 'properties': {'selected': '/value/0'}}}}
        for locale in ('en','fr'):
            report, workspace = root/(locale+'.json'), root/(locale+'-workspace.json')
            p = subprocess.run([desktop,'--smoke','--no-preferences','--no-restore-session','--locale',locale,'--container',str(before),'--container-entry',entry,'--after-container',str(root/'attributes-renamed-changed.utoc'),'--after-container-entry',entry,'--container-tab','meshes','--report',str(report),'--save-workspace',str(workspace),*map(str,flags),'--document-action','view-state:'+json.dumps(state,separators=(',',':'))],env=env,capture_output=True,timeout=45)
            assert p.returncode == 0,p.stderr.decode('utf-8','replace')
            d = json.loads(report.read_text(encoding='utf-8'));views=d['containerView']['meshViews']
            assert d['locale'] == locale and d['document']['container']['meshDifferences'][0]['pairing'] == 'explicit-members-single-mesh-lod'
            assert len(views) == 2 and all(v['comparisonKnown'] and v['attributesAvailable'] and v['attributes']['comparing'] and v['attributes']['modified'] == 1 for v in views)
            assert views[0]['attributes']['selected'] == 0 and views[0]['attributes']['selectedVisible']
            saved = json.loads(workspace.read_text(encoding='utf-8'))['documents'][0]['viewState']['container']
            assert saved['meshAttributes'] and saved['meshAttributeView']['channel'] == 'Texture' and saved['meshAttributeView']['vertex'] == 0 and saved['meshAttributeView']['properties']['selected'] == '/value/0'
            checks += 5
    for name in ('attributes.utoc','attributes.ucas','attributes-changed.utoc','attributes-changed.ucas','global.utoc','global.ucas','native.usmap','after.usmap','context.json','after-context.json'):
        run('zen-skeletal-diff',before,entry,after,entry,'--after-schema',root/'after-context.json','--out',root/name,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p,h in protected.items())
    checks += 1
print(f'{checks} vertex attribute CLI/desktop checks; protected inputs unchanged')
