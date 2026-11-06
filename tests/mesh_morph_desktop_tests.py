"""Verify native morph previews and persisted bilingual typed source inspection."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe, fixture = (str(Path(p).resolve()) for p in sys.argv[1:])
checks = 0
env = {k.upper(): v for k, v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-morph-desktop-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    env['LOCALAPPDATA'] = str(root / 'unused-profile')
    subprocess.run([fixture, '--write-fixtures', str(root)], capture_output=True, check=True, env=env, timeout=30)
    hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.rglob('*') if p.is_file()}
    report, workspace = root / 'report.json', root / 'workspace.json'
    def run(*args):
        global checks
        report.unlink(missing_ok=True)
        result = subprocess.run([exe, '--smoke', '--no-preferences', '--report', str(report), *map(str, args)], capture_output=True, env=env, timeout=45)
        assert result.returncode == 0, (result.returncode, result.stderr.decode('utf-8', 'replace'), args)
        checks += 1
        return json.loads(report.read_text(encoding='utf-8'))
    state = {'asset': {'meshMorphInspection': True, 'meshMorphs': {'filter': 'Smile', 'selected': '/targets/Smile/positions/0/0', 'changedOnly': True}}}
    first = run('--before', root / 'morph-before.uasset', '--after', root / 'morph-after.uasset', '--view', 'meshes', '--document-action', 'view-state:' + json.dumps(state, separators=(',', ':')), '--save-workspace', workspace)
    assert first['document']['beforeMesh'] == {'vertices': 3, 'triangles': 1, 'morphTargets': 2}
    assert first['document']['afterMesh'] == first['document']['beforeMesh']
    actual = first['workspace']['documents'][0]['viewState']['asset']
    assert actual['meshMorphInspection'] and actual['meshMorphs']['filter'] == 'Smile' and actual['meshMorphs']['changedOnly']
    restored = run('--workspace', workspace, '--locale', 'fr')
    assert restored['document'] == first['document']
    assert restored['workspace']['documents'][0]['viewState']['asset']['meshMorphs'] == actual['meshMorphs']
    virtual = run('--before', root / 'morph-virtualized.uasset', '--payload-root', root / 'payloads', '--view', 'meshes')
    assert virtual['document']['beforeMesh']['morphTargets'] == 2
    for path, digest in hashes.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
print(f'{checks} source morph Desktop checks passed')
