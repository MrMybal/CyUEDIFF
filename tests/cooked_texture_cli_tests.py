"""Cooked pixel CLI, external-only differences, guarded outputs and optional desktop binding."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:3]]
desktop = str(Path(sys.argv[3]).resolve()) if len(sys.argv) > 3 else None
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-pixels-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], check=True, env=env, capture_output=True, timeout=20)
    originals = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    common = ['--cooked-profile', 'ue5.3', '--script-objects', str(root/'global.utoc'), '--schema', str(root/'context.json')]
    entry = '000000000000000000000001'
    def run(command, *arguments, code=0):
        global checks
        p = subprocess.run([cli, command, *map(str, arguments), *common], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, p.stderr.decode('utf-8', 'replace')
        checks += 1
        return json.loads(p.stdout) if code != 2 and p.stdout else p.stderr.decode('utf-8', 'replace')
    inline, external, changed = [root/(name+'.utoc') for name in ('inline', 'external', 'changed')]
    for name, container in [('inline', inline), ('external', external)]:
        metadata = run('zen-texture', container, entry)
        mip = metadata['platforms'][0]['mips'][0]
        assert mip['storage'] == name and mip['loaded'] and len(mip['sha256']) == 64
        checks += 1
        image = root/(name+'.png')
        report = run('zen-preview', container, entry, '--out', image)
        assert struct.unpack_from('>II', image.read_bytes(), 16) == (4, 4) and report['width'] == 4
        checks += 1
    compare = run('zen-image-diff', inline, entry, external, entry)
    assert compare['status'] == 'identical' and compare['changedPixels'] == 0
    assert compare['before']['width'] == 4 and compare['beforeTexture']['platforms'][0]['format'] == 'PF_DXT1'
    checks += 1
    # The package itself is byte-identical. Only its external bulk pixels changed.
    assert run('zen-diff', external, entry, changed, entry)['status'] == 'identical'
    compare = run('zen-image-diff', external, entry, changed, entry, '--heatmap', root/'mask.png', code=1)
    assert compare['changedPixels'] == 16 and compare['changedBounds'] == {'x': 0, 'y': 0, 'width': 4, 'height': 4}
    checks += 1
    assert run('zen-image-diff', external, entry, changed, entry, '--threshold', 255)['changedPixels'] == 0
    compare = run('zen-hdr-diff', external, entry, changed, entry, code=1)
    assert compare['changedPixels'] == 16 and compare['maxSampleDelta'] == 1
    checks += 1
    assert 'container-integrity' in run('zen-preview', root/'corrupt.utoc', entry, '--out', root/'never.png', code=2)
    assert not (root/'never.png').exists()
    checks += 1
    for options in [[], ['--out', external], ['--out', root/'context.json'], ['--out', root/'native.usmap'],
                    ['--out', root/'bad.png', '--mip', 1], ['--out', root/'bad.png', '--platform', 1],
                    ['--out', root/'bad.png', '--export-index', 1], ['--out', root/'bad.png', '--exposure', 1]]:
        run('zen-preview', external, entry, *options, code=2)
    run('zen-texture', external, entry, '--mip', 1, code=2)
    if desktop:
        for lang in ('en', 'fr'):
            report = root/(lang+'.json')
            p = subprocess.run([desktop, '--smoke', '--no-preferences', '--locale', lang,
                '--container', str(external), '--container-entry', entry, '--after-container', str(changed),
                '--after-cooked-profile', 'ue5.3', '--after-script-objects', str(root/'global.utoc'),
                '--after-schema', str(root/'context.json'), '--container-tab', 'pixels', '--report', str(report), '--save-workspace', str(root/'pixels-workspace.json'), *common],
                env=env, capture_output=True, timeout=45)
            assert p.returncode == 0, p.stderr.decode('utf-8', 'replace')
            data = json.loads(report.read_text(encoding='utf-8'))
            container = data['document']['container']
            assert len(container['texturePreviews']) == 1 and len(container['after']['texturePreviews']) == 1
            assert container['textureDifferences'][0]['comparison']['changedPixels'] == 16
            assert data['containerView']['tab'] == 5 and data['containerView']['pixelViews'] == 2
            checks += 4
        reopened = root/'reopened.json'
        p = subprocess.run([desktop, '--smoke', '--no-preferences', '--workspace', str(root/'pixels-workspace.json'),
            '--report', str(reopened)], env=env, capture_output=True, timeout=45)
        assert p.returncode == 0, p.stderr.decode('utf-8', 'replace')
        restored = json.loads(reopened.read_text(encoding='utf-8'))
        assert restored['containerView']['tab'] == 5 and restored['containerView']['pixelViews'] == 2
        checks += 2
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == digest for p, digest in originals.items())
    checks += 1
print(f'{checks} cooked pixel CLI checks passed')
