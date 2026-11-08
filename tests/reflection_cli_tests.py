"""Public inspect/diff contracts for independently encoded struct declarations."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-reflection-cli-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixture, '--write-fixtures', temp], check=True, capture_output=True, timeout=30)
    hashes = {p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    def run(*args, code=0):
        global checks
        result = subprocess.run([cli, *map(str, args)], capture_output=True, timeout=30)
        assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
        checks += 1
        return json.loads(result.stdout) if result.stdout else None
    before, after, split = [root/(name+'.uasset') for name in ('before','after','split')]
    for file in (before, split):
        inspected = run('inspect', file)
        declaration = inspected['reflection']['definitions']['TestAsset']['declaration']
        assert inspected['reflection']['complete']
        assert declaration == inspected['snapshot']['objects']['TestAsset']['native']
        assert inspected['coverage']['TestAsset']['nativeCoverage'] == 'struct-declaration-prefix'
        assert declaration['fields']['Value']['type'] == ('ArrayProperty' if file == split else 'IntProperty')
        checks += 4
        assert not run('diff', file, file)['changes']
    changed = run('diff', before, after, code=1)
    rows = [v for v in changed['changes'] if v['path'].endswith('/native/fields/Value/metadata/DisplayName')]
    assert len(rows) == 1 and rows[0]['before'] == 'Visible value' and rows[0]['after'] == 'Edited value'
    checks += 1
    output = root/'report.json'
    run('inspect', before, '--out', output)
    assert json.loads(output.read_text(encoding='utf-8'))['reflection']['complete']
    for path, digest in hashes.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
        checks += 1
print(f'{checks} reflection CLI checks passed')
