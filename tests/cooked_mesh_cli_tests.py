"""Native render geometry, external-only diffs, output protection and desktop retention."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-mesh-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], check=True, env=env, capture_output=True, timeout=20)
    originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    common = ['--cooked-profile', 'ue5.3', '--script-objects', str(root/'global.utoc'), '--schema', str(root/'context.json')]
    native = ['--mesh-profile', 'ue5.3-windows', '--keep-mobile-min-lod', '0']
    entry = '000000000000000000000001'
    def run(command, *arguments, code=0, profile=True):
        global checks
        p = subprocess.run([cli, command, *common, *(native if profile else []), *map(str, arguments)], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, p.stderr.decode('utf-8', 'replace')
        checks += 1
        return json.loads(p.stdout) if code != 2 and p.stdout else p.stderr.decode('utf-8', 'replace')
    inline, external, changed = [root/(name+'.utoc') for name in ('inline', 'external', 'changed')]
    for container in (inline, external):
        metadata = run('zen-mesh', container, entry)
        assert metadata['tail']['decoded'] is False and metadata['tail']['bytes'] == 1
        assert metadata['lods'][0]['geometryAvailable'] is False
        geometry = run('zen-geometry', container, entry)
        assert len(geometry['vertices']) == 3 and len(geometry['triangles']) == 1
        assert geometry['instances']['1']['uvs'] == [[1.0, 0.0]]
        checks += 2
        obj = root/('export-'+container.stem+'.obj')
        run('zen-export-mesh', container, entry, '--out', obj)
        assert obj.exists() and len([l for l in obj.read_text().splitlines() if l.startswith('f ')]) == 1
        checks += 1
    assert run('zen-mesh-diff', inline, entry, external, entry)['status'] == 'identical'
    assert run('zen-diff', external, entry, changed, entry, profile=False)['status'] == 'identical'
    diff = run('zen-mesh-diff', external, entry, changed, entry, code=1)
    assert diff['status'] == 'changed' and diff['beforeMesh']['packageSha256'] == diff['afterMesh']['packageSha256']
    assert diff['beforeMesh']['lods'][0]['bulk']['sha256'] != diff['afterMesh']['lods'][0]['bulk']['sha256']
    checks += 2
    assert 'cooked-mesh-profile' in run('zen-geometry', external, entry, profile=False, code=2)
    for args in [[], ['--out', external], ['--out', root/'context.json'], ['--out', root/'native.usmap'],
                 ['--out', root/'bad.obj', '--lod', '1'], ['--out', root/'bad.obj', '--export-index', '1']]:
        run('zen-export-mesh', external, entry, *args, code=2)
    assert 'container-integrity' in run('zen-export-mesh', root/'corrupt.utoc', entry, '--out', root/'never.obj', code=2)
    assert not (root/'never.obj').exists()
    checks += 1
    run('zen-mesh', external, entry, '--lod', '0', code=2)
    run('zen-geometry', external, entry, '--after-lod', '0', code=2)
    run('zen-geometry', external, entry, '--keep-mobile-min-lod', '2', code=2)
    if desktop:
        for lang in ('en', 'fr'):
            report = root/(lang+'.json')
            p = subprocess.run([desktop, '--smoke', '--no-preferences', '--locale', lang,
                '--container', str(external), '--container-entry', entry, '--after-container', str(changed),
                '--after-cooked-profile', 'ue5.3', '--after-script-objects', str(root/'global.utoc'),
                '--after-schema', str(root/'context.json'), '--after-mesh-profile', 'ue5.3-windows',
                '--after-keep-mobile-min-lod', '0', '--container-tab', 'meshes', '--report', str(report),
                '--save-workspace', str(root/'workspace.json'), *common, *native], env=env, capture_output=True, timeout=45)
            assert p.returncode == 0, p.stderr.decode('utf-8', 'replace')
            data = json.loads(report.read_text(encoding='utf-8'))
            document = data['document']['container']
            assert document['meshPreviews'] == document['after']['meshPreviews'] == 1
            assert document['meshDifferences'][0]['comparison']['status'] == 'changed'
            assert data['containerView']['tab'] == 6 and len(data['containerView']['meshViews']) == 2
            assert sum(v['changes']['modified'] for v in data['containerView']['meshViews']) > 0
            checks += 4
        reopened = root/'reopened.json'
        p = subprocess.run([desktop, '--smoke', '--no-preferences', '--workspace', str(root/'workspace.json'),
            '--report', str(reopened)], env=env, capture_output=True, timeout=45)
        assert p.returncode == 0, p.stderr.decode('utf-8', 'replace')
        restored = json.loads(reopened.read_text(encoding='utf-8'))
        assert restored['containerView']['tab'] == 6 and len(restored['containerView']['meshViews']) == 2
        checks += 2
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in originals.items())
    checks += 1
print(f'{checks} cooked mesh CLI checks passed')
