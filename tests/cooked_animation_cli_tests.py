"""Cooked AnimSequence CLI: verified dependencies, fractional samples, unavailable peers and input protection."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:3]]
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-cooked-animation-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', str(root)], check=True, env=env, capture_output=True, timeout=30)
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--cooked-profile', 'ue5.3', '--animation-profile', 'ue5.3-windows',
              '--script-objects', str(root/'global.utoc'), '--schema', str(root/'context.json'),
              '--bone-settings-container', str(root/'settings.utoc'), '--bone-settings-entry', entry,
              '--skeleton-container', str(root/'skeleton.utoc'), '--skeleton-entry', entry]
    after = ['--after-bone-settings-container', str(root/'settings.utoc'),
             '--after-skeleton-container', str(root/'skeleton.utoc')]

    def run(command, *arguments, code=0, context=common):
        result = subprocess.run([cli, command, *map(str, context), *map(str, arguments)], env=env,
                                capture_output=True, text=True, encoding='utf-8', timeout=30)
        assert result.returncode == code, (command, result.returncode, result.stdout[:1500], result.stderr)
        return json.loads(result.stdout) if result.stdout.strip() else result.stderr

    source = root/'animation.utoc'
    info = run('zen-animation', source, entry)
    assert info['boneChannelsDecoded'] and info['identitiesComplete']
    assert info['boneCodec']['codec']['class'] == '/Script/Engine.AnimCompress_PerTrackCompression'
    assert info['skeleton']['status'] == 'verified' and len(info['skeleton']['bones']) == 2
    assert 'values' not in info['animation']['tracks'][0]['translation']
    keys = run('zen-animation', source, entry, '--include-keys', 1)
    assert keys['animation']['tracks'][0]['translation']['values'] == [[2., 0., 0.], [10., 0., 0.]]
    checks += 5
    sample = run('zen-animation-frame', source, entry, '--frame', '2.5')
    assert sample['frame'] == 2.5 and sample['tracks']['name:Child']['positions'][0] == 7.
    assert not sample['skeleton'].get('bones') and len(sample['numericTracks']) == 1
    checks += 2
    identical = run('zen-animation-frame-diff', source, entry, source, entry, *after,
                    '--before-frame', '2.5', '--after-frame', '2.5')
    assert identical['status'] == 'identical' and identical['before']['frame'] == 2.5
    changed = run('zen-animation-frame-diff', source, entry, source, entry, *after,
                  '--before-frame', 0, '--after-frame', '.5', code=1)
    assert changed['tracks']['name:Child']['metrics']['positionDistance'] == 1.
    tolerant = run('zen-animation-frame-diff', source, entry, source, entry, *after,
                   '--before-frame', 0, '--after-frame', '.5', '--position-tolerance', 1)
    assert tolerant['status'] == 'identical'
    changed = run('zen-animation-frame-diff', source, entry, root/'changed.utoc', entry, *after,
                  '--before-frame', '2.5', '--after-frame', '2.5', code=1)
    assert changed['status'] == 'changed' and changed['beforeAnimation']['source']['packageSha256'] != changed['afterAnimation']['source']['packageSha256']
    assert changed['beforeAnimation']['boneCodec']['settings']['packageSha256'] == changed['afterAnimation']['boneCodec']['settings']['packageSha256']
    checks += 5
    external = run('zen-animation', root/'external.utoc', entry)
    assert external['compressedBuffer']['bulk']['storage'] == 'external' and external['animation']['bufferSha256'] == info['animation']['bufferSha256']
    missing = run('zen-animation', root/'missing.utoc', entry)
    assert not missing['boneChannelsDecoded'] and missing['unavailable'] == 'missing-chunk'
    unavailable = run('zen-animation-frame-diff', source, entry, root/'missing.utoc', entry, *after, code=2)
    assert unavailable['status'] == 'incomplete' and unavailable['changedTracks'] == 0
    assert unavailable['tracks']['name:Child']['status'] == 'unavailable'
    checks += 4
    mode = run('zen-animation-frame', root/'no-interpolation.utoc', entry, '--frame', '2.5', code=2)
    assert not mode['ready']
    mode = run('zen-animation-frame', root/'no-interpolation.utoc', entry, '--frame', '2.5', '--interpolation', 'step')
    assert mode['tracks']['name:Child']['positions'][0] == 2.
    assert 'cooked-animation-context' in run('zen-animation-frame', source, entry, '--interpolation', 'step', code=2)
    checks += 3
    no_dependency = common[:common.index('--bone-settings-container')]
    assert run('zen-animation', source, entry, context=no_dependency)['unavailable'] == 'bone-compression-settings-not-bound'
    assert 'arguments' in run('zen-animation', source, entry, '--animation-profile', 'guess', code=2)
    for flag, value in (('--frame', '-1'), ('--frame', 'nan'), ('--frame', '5')):
        assert run('zen-animation-frame', source, entry, flag, value, code=2)
    checks += 5
    for name in ('animation.utoc', 'animation.ucas', 'settings.utoc', 'settings.ucas',
                 'skeleton.utoc', 'skeleton.ucas', 'global.utoc', 'global.ucas', 'context.json', 'mappings.usmap'):
        assert 'output' in str(run('zen-animation', source, entry, '--out', root/name, code=2)).lower()
        checks += 1
    for path, digest in protected.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
    print(f'{checks} cooked animation CLI checks; protected inputs unchanged')
