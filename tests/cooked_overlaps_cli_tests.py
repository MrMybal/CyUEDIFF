"""Native overlap relations reach CLI and bilingual wire diffs without modifying inputs."""
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
with tempfile.TemporaryDirectory(prefix='.test-tmp-overlaps-', dir=Path.cwd()) as temp:
    root = Path(temp)
    for exe in (base, fixture):
        subprocess.run([exe, '--write-fixtures', str(root)], check=True, capture_output=True, env=env)
    protected = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry = '000000000000000000000001'
    common = ['--cooked-profile', 'ue5.3', '--script-objects', root/'global.utoc', '--schema', root/'context.json', '--skeletal-profile', 'ue5.3-windows', '--keep-mobile-min-lod', '0', '--has-vertex-colors', '0']
    def run(command, *args, code=0):
        global checks
        p = subprocess.run([cli, command, *map(str, common), *map(str, args)], env=env, capture_output=True, timeout=30)
        assert p.returncode == code, (p.returncode, p.stderr.decode('utf-8', 'replace'))
        checks += 1
        return json.loads(p.stdout) if p.stdout else None
    before, after = root/'overlaps.utoc', root/'overlaps-changed.utoc'
    model = run('zen-skeletal', before, entry)
    raw = model['lods'][0]['sections'][0]['vertexOverlaps']
    assert raw['indices'] == [1, 0] and raw['windows'] == [[1,0],[1,1],[0,2]]
    assert not raw['runtimeTangentsEvaluated'] and not raw['coincidenceVerified']
    diff = run('zen-skeletal-diff', before, entry, after, entry, code=1)
    assert diff['status'] == 'changed' and diff['changes'][0]['path'] == '/vertexOverlaps/sections/0/vertices/0/0'
    assert run('zen-skeletal-diff', before, entry, root/'overlaps-repacked.utoc', entry)['status'] == 'identical'
    unknown = run('zen-skeletal-diff', before, entry, root/'overlaps-stripped.utoc', entry, code=2)
    assert unknown['status'] == 'incomplete' and unknown['changes'] == [] and unknown['diagnostics']
    assert run('zen-skeletal-diff', before, entry, root/'overlaps-empty.utoc', entry, code=1)['status'] == 'changed'
    checks += 5
    if desktop:
        flags = [*common]
        flags[flags.index('--keep-mobile-min-lod')] = '--skeletal-keep-mobile-min-lod'
        flags += ['--after-cooked-profile','ue5.3','--after-script-objects',root/'global.utoc','--after-schema',root/'context.json','--after-skeletal-profile','ue5.3-windows','--after-skeletal-keep-mobile-min-lod','0','--after-has-vertex-colors','0']
        for locale in ('en','fr'):
            report, workspace = root/(locale+'.json'), root/(locale+'-workspace.json')
            p = subprocess.run([desktop,'--smoke','--no-preferences','--no-restore-session','--locale',locale,'--container',str(before),'--container-entry',entry,'--after-container',str(after),'--after-container-entry',entry,'--container-tab','meshes','--report',str(report),'--save-workspace',str(workspace),*map(str,flags)],env=env,capture_output=True,timeout=45)
            assert p.returncode == 0,p.stderr.decode('utf-8','replace')
            d = json.loads(report.read_text(encoding='utf-8'))
            assert d['locale'] == locale and d['document']['container']['meshDifferences'][0]['comparison']['status'] == 'changed'
            assert all(v['comparisonKnown'] and v['changes']['modified'] == 2 for v in d['containerView']['meshViews'])
            assert json.loads(workspace.read_text(encoding='utf-8'))['schema'] == 'cyuediff.workspace/13'
            checks += 4
    for name in ('overlaps.utoc','overlaps.ucas','overlaps-changed.utoc','overlaps-changed.ucas','global.utoc','global.ucas','native.usmap','context.json'):
        run('zen-skeletal-diff',before,entry,after,entry,'--out',root/name,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p,h in protected.items())
    checks += 1
print(f'{checks} native overlap CLI/desktop checks; protected inputs unchanged')
