"""Mapped cooked collision geometry, named diffs, protected writes and prepared native review."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-collision-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], env=env, check=True, capture_output=True, timeout=20)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--cooked-profile', 'ue5.3', '--schema', str(root/'context.json'), '--script-objects', str(root/'global.utoc')]
    def run(command, *args, code=0):
        global checks
        p = subprocess.run([cli, command, *map(str,args), *common], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, p.stderr.decode('utf-8','replace')
        checks += 1
        return json.loads(p.stderr if p.stderr else p.stdout)
    before, after = root/'collision.utoc', root/'changed.utoc'
    projection = run('zen-collision', before, entry)
    body = '/Game/Texture.Texture'
    assert projection['complete'] and projection['bodies'][body]['shapes']['sphere:index:0']['geometry']['radius'] == 4.25
    assert projection['provenance']['mappingVerified'] is False and len(projection['provenance']['scriptObjectsSha256']) == 64
    checks += 2
    wire = run('zen-collision-wire', before, entry)
    assert wire['sourceComplete'] and len(wire['shapes'][0]['lines']) == 96
    checks += 1
    assert run('zen-collision-diff', before, entry, before, entry)['status'] == 'identical'
    difference = run('zen-collision-diff', before, entry, after, entry, code=1)
    assert difference['complete'] and difference['status'] == 'changed'
    assert any(c['path'].endswith('/geometry/radius') and c['before'] == 4.25 and c['after'] == 8.25 for c in difference['changes'])
    assert difference['beforeContext']['packageSha256'] != difference['afterContext']['packageSha256']
    checks += 3
    unavailable = run('zen-collision', root/'nan.utoc', entry)
    assert not unavailable['complete'] and unavailable['bodies'][body]['shapes'] == {}
    assert unavailable['bodies'][body]['native']['opaque']['propertyRead']['errorOffset'] > 0
    checks += 2
    negative = run('zen-collision', root/'negative.utoc', entry)
    assert not negative['complete'] and 'radius' not in negative['bodies'][body]['shapes']['sphere:index:0']['geometry']
    checks += 1
    assert run('zen-collision-diff', before, entry, root/'negative.utoc', entry, code=2)['status'] == 'incomplete'
    for target in (before, root/'collision.ucas', root/'context.json', root/'native.usmap'):
        assert run('zen-collision', before, entry, '--out', target, code=2)['error'] == 'output-path'
    assert run('zen-collision', root/'corrupt.utoc', entry, '--out', root/'never.json', code=2)['error'] == 'container-integrity'
    assert not (root/'never.json').exists()
    checks += 1
    run('zen-collision', before, entry, '--lod', '0', code=2)
    # A report alias must never overwrite a corpus input, even during failure cleanup.
    with tempfile.TemporaryDirectory(prefix='build-collision-report-guard-', dir=Path.cwd()) as guard_temp:
        guard = Path(guard_temp).resolve()
        assert guard.is_relative_to(Path.cwd().resolve())
        keep = root/'guard-input.bin'
        keep.write_bytes(b'corpus-input')
        os.link(keep, guard/'summary.json')
        pairs = root/'guard-pairs.json'
        pairs.write_text(json.dumps([{'name':'/Game/Guard','id':entry,'source':str(keep)}]),encoding='utf-8')
        validator = Path(__file__).resolve().parents[1]/'scripts/validate_cooked_collisions.py'
        p = subprocess.run([sys.executable,str(validator),'--pairs',str(pairs),'--container',str(before),
            '--global-toc',str(root/'global.utoc'),'--schema',str(root/'context.json'),'--consumer',cli,
            '--cli',cli,'--output-dir',str(guard)],env=env,capture_output=True,timeout=30)
        assert p.returncode == 2 and b'Output aliases an input' in p.stderr and keep.read_bytes() == b'corpus-input'
        checks += 1
    if desktop:
        selection = {'container': {'collisions': {'body': body, 'shape': 'sphere:index:0',
            'viewport': {'target': [1.,2.,3.], 'up': [0.,0.,1.], 'yaw': .3, 'pitch': .2, 'distance': 30., 'changedOnly': True}}}}
        for lang in ('en','fr'):
            report, workspace = root/(lang+'.json'), root/(lang+'-workspace.json')
            p = subprocess.run([desktop, '--smoke', '--no-preferences', '--locale', lang, '--container', str(before),
                '--container-entry', entry, '--after-container', str(after), '--after-cooked-profile', 'ue5.3',
                '--after-schema', str(root/'context.json'), '--after-script-objects', str(root/'global.utoc'),
                '--container-tab', 'collisions', '--document-action', 'view-state:'+json.dumps(selection,separators=(',',':')),
                '--report', str(report), '--save-workspace', str(workspace), *common], env=env, capture_output=True, timeout=45)
            assert p.returncode == 0, p.stderr.decode('utf-8','replace')
            data = json.loads(report.read_text(encoding='utf-8'))
            model = data['document']['container']
            assert model['collisionEdges'] == model['after']['collisionEdges'] == 96
            assert model['collisionDifference']['status'] == 'changed'
            review = data['containerView']['collisionView']
            assert data['containerView']['tab'] == 7 and review['changes']['modified'] == 1 and review['beforeEdges'] == review['afterEdges'] == 96
            saved = json.loads(workspace.read_text(encoding='utf-8'))
            assert saved['documents'][0]['viewState']['container']['collisions']['shape'] == 'sphere:index:0'
            assert saved['documents'][0]['viewState']['container']['collisions']['viewport']['yaw'] == .3
            checks += 5
        p = subprocess.run([desktop, '--smoke', '--no-preferences', '--workspace', str(root/'fr-workspace.json'),
            '--report', str(root/'reopened.json')], env=env, capture_output=True, timeout=45)
        assert p.returncode == 0, p.stderr.decode('utf-8','replace')
        reopened = json.loads((root/'reopened.json').read_text(encoding='utf-8'))
        assert reopened['containerView']['tab'] == 7 and reopened['containerView']['collisionView']['changes']['modified'] == 1
        checks += 1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p,h in hashes.items())
    checks += 1
print(f'{checks} cooked collision CLI checks passed')
