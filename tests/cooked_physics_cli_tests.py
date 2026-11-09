"""Original native Chaos fixture/CLI contracts; no engine data."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
cli, fixture = [Path(p).resolve() for p in sys.argv[1:3]]
desktop = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0

def run(args, code=0):
    global checks
    p = subprocess.run(list(map(str, args)), capture_output=True, env=env, timeout=45)
    assert p.returncode == code, (p.returncode, p.stderr.decode('utf-8', 'replace'))
    checks += 1
    return json.loads(p.stdout) if p.stdout else None
with tempfile.TemporaryDirectory(prefix='cyuediff-physics-cli-') as directory:
    root = Path(directory)
    run([fixture, '--write-fixtures', root])
    entry = '000000000000000000000001'
    context = ['--cooked-profile', 'ue5.3', '--physics-profile', 'ue5.3-windows', '--script-objects', root / 'global.utoc', '--schema', root / 'context.json']
    a = run([cli, 'zen-physics', root / 'physics.utoc', entry, *context])
    b = run([cli, 'zen-physics', root / 'external.utoc', entry, *context])
    assert a['complete'] and len(a['bodies'][0]['formats'][0]['physics']['objects']) == 2
    assert b['bodies'][0]['formats'][0]['storage'] == 'external'
    same = run([cli, 'zen-physics-diff', root / 'physics.utoc', entry, root / 'external.utoc', entry, *context])
    assert same['status'] == 'unchanged' and (not same['changes'])
    diff = run([cli, 'zen-physics-diff', root / 'external.utoc', entry, root / 'changed.utoc', entry, *context], 1)
    assert diff['status'] == 'changed' and len(diff['changes']) == 1
    assert diff['changes'][0]['before'] == 0 and diff['changes'][0]['after'] == 0.5
    wire = run([cli, 'zen-physics-wire', root / 'physics.utoc', entry, *context])
    assert wire['space'] == 'cooker' and sum(len(s['lines']) for s in wire['shapes']) == 15
    assert wire['shapes'][0]['collection'] == 'chaos/PF_DXT1#0/convex'
    target = root / 'report.json'
    run([cli, 'zen-physics', root / 'physics.utoc', entry, *context, '--out', target])
    assert json.loads(target.read_text(encoding='utf-8')) == a
    for extra in (['--physics-profile', 'unknown'], ['--after-physics-profile', 'ue5.3-windows'], ['--lod', '0'], ['--export-index', '0'], ['--mesh-profile', 'ue5.3-windows'], ['--cooked-profile', 'ue5.5']):
        run([cli, 'zen-physics', root / 'physics.utoc', entry, *context, *extra], 2)
    run([cli, 'zen-physics', root / 'corrupt.utoc', entry, *context], 2)
    run([cli, 'zen-physics', root / 'physics.utoc', entry], 2)
    run([cli, 'zen-physics-diff', root / 'external.utoc', entry, root / 'changed.utoc', entry, *context, '--after-cooked-profile', 'ue5.5'], 2)
    for source in (root / 'physics.utoc', root / 'physics.ucas', root / 'global.utoc', root / 'global.ucas', root / 'native.usmap', root / 'context.json'):
        before = hashlib.sha256(source.read_bytes()).hexdigest()
        run([cli, 'zen-physics', root / 'physics.utoc', entry, *context, '--out', source], 2)
        assert hashlib.sha256(source.read_bytes()).hexdigest() == before
    if desktop:
        hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.suffix in ('.utoc','.ucas','.usmap') or p.name == 'context.json'}
        body = '/Game/Texture.Texture'
        shape = 'chaos/PF_DXT1#0/convex/index:0'
        selection = {'container': {'physics': {'body': body, 'shape': shape,
            'viewport': {'target': [.25,.25,.25], 'up': [0.,0.,1.], 'yaw': .3, 'pitch': .2, 'distance': 4., 'changedOnly': True}}}}
        for lang in ('en','fr'):
            report, workspace = root/(lang+'.json'), root/(lang+'-workspace.json')
            run([desktop, '--smoke', '--no-preferences', '--locale', lang,
                 '--container', root/'physics.utoc', '--container-entry', entry,
                 '--after-container', root/'changed.utoc', '--after-physics-profile', 'ue5.3-windows',
                 '--after-cooked-profile', 'ue5.3', '--after-schema', root/'context.json',
                 '--after-script-objects', root/'global.utoc', '--container-tab', 'physics',
                 '--document-action', 'view-state:'+json.dumps(selection,separators=(',',':')),
                 '--report', report, '--save-workspace', workspace, *context])
            data=json.loads(report.read_text(encoding='utf-8'))
            model=data['document']['container'];review=data['containerView']['physicsView']
            assert model['physics']['complete'] and model['physicsEdges']==model['after']['physicsEdges']==15
            assert model['physicsDifference']['status']=='changed'
            assert data['containerView']['tab']==8 and review['shape']==shape and review['beforeEdges']==review['afterEdges']==6
            saved=json.loads(workspace.read_text(encoding='utf-8'))
            assert saved['schema']=='cyuediff.workspace/13' and saved['documents'][0]['settings']['physics_profile']==1
            assert saved['documents'][0]['settings']['after_physics_profile']==1
            assert saved['documents'][0]['viewState']['container']['physics']['viewport']['yaw']==.3
        run([desktop,'--smoke','--no-preferences','--workspace',root/'fr-workspace.json','--report',root/'reopened.json'])
        reopened=json.loads((root/'reopened.json').read_text(encoding='utf-8'))
        assert reopened['containerView']['tab']==8 and reopened['containerView']['physicsView']['shape']==shape
        run([desktop,'--smoke','--no-preferences','--container',root/'physics.utoc','--container-entry',entry,
             '--after-container',root/'changed.utoc','--after-cooked-profile','ue5.3','--after-schema',root/'context.json',
             '--after-script-objects',root/'global.utoc','--container-tab','physics','--report',root/'unknown-after.json',*context])
        unknown=json.loads((root/'unknown-after.json').read_text(encoding='utf-8'))['containerView']['physicsView']
        assert not unknown['complete'] and unknown['changes']['removed']==0 and unknown['changes']['modified']==0
        assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
print(f'{checks} cooked physics CLI checks passed')
