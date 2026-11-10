"""Synthetic source files exercise explicit prepared defaults and effective CLI differences."""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import sys
import tempfile

exe, fixture = (str(Path(p).resolve()) for p in sys.argv[1:3])
checks = 0
env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-effective-cli-', dir=Path.cwd()) as folder:
    root = Path(folder)
    subprocess.run([fixture, '--emit-fixtures', str(root)], env=env, check=True, capture_output=True)
    original = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}

    def run(*args, code=0):
        global checks
        p = subprocess.run([exe, *map(str, args)], env=env, capture_output=True, timeout=45)
        assert p.returncode == code, (args, p.returncode, p.stderr.decode('utf-8', 'replace')[:1500])
        checks += 1
        return json.loads(p.stderr or p.stdout) if p.stderr or p.stdout else None

    context = root/'context.json'
    before, after, named = (root/(name+'.uasset') for name in ['before', 'after', 'named'])
    inspected = run('effective-inspect', before, context)
    assert inspected['effective']['complete'] and inspected['stored']['objects']['TestAsset']['properties'] == {}
    count = '/Script/Test.Child.Count'
    assert inspected['effective']['fields'][count]['value'] == 41
    report = run('effective-inspect', after, context)
    assert report['effective']['fields'][count]['value'] == 99
    options = ['--before-context', context, '--after-context', context]
    assert run('effective-diff', before, before, *options)['comparison']['status'] == 'identical'
    changed = run('effective-diff', before, after, *options, code=1)['comparison']
    assert len(changed['changes']) == 1 and changed['changes'][0]['before'] == 41 and changed['changes'][0]['after'] == 99
    changed = run('effective-diff', before, named, *options, code=1)['comparison']
    assert len(changed['changes']) == 1 and changed['changes'][0]['after'] == {'base': 'Item', 'number': 2}
    digest = hashlib.sha256(context.read_bytes()).hexdigest()
    assert run('effective-inspect', before, context, '--expect-context', digest)['effective']['complete']
    run('effective-inspect', before, context, '--expect-context', 'f'*64, code=2)
    run('effective-diff', before, before, *options, '--expect-after-context', 'f'*64, code=2)
    run('effective-inspect', before, context, '--object', 'wrong', code=2)
    for args in [('effective-diff', before, after), ('effective-inspect', before),
                 ('effective-inspect', before, context, '--metadata'),
                 ('effective-inspect', before, context, '--object', 'TestAsset', '--object', 'TestAsset')]:
        run(*args, code=2)
    for target in [before, context, root/'capture.json', root/'input.dat']:
        assert run('effective-inspect', before, context, '--out', target, code=2)['error'] == 'output-path'
    link = root/'capture-alias.json'
    try:
        os.link(root/'capture.json', link)
    except OSError:
        pass
    else:
        run('effective-inspect', before, context, '--out', link, code=2)
    target = root/'report.json'
    run('effective-inspect', before, context, '--out', target)
    assert json.loads(target.read_text()) == inspected
    run('effective-inspect', before, context, '--out', target, code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in original.items())
    # Even an updated manifest cannot pair old captured CDO bytes with a new module input.
    input_file = root/'input.dat'
    input_file.write_bytes(b'Changed recorded input!')
    doc = json.loads(context.read_text())
    for dep in doc['dependencies'].values():
        if dep.get('file') == str(input_file):
            dep.update(bytes=input_file.stat().st_size, sha256=hashlib.sha256(input_file.read_bytes()).hexdigest())
    poisoned = root/'poisoned-context.json'
    poisoned.write_text(json.dumps(doc), encoding='utf-8')
    error = run('effective-inspect', before, poisoned, code=2)
    assert error['error'] == 'default-context'
    input_file.write_bytes(b'Original recorded input')
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == h for p, h in original.items())
print(f'{checks} effective default CLI checks passed; original synthetic inputs unchanged')
