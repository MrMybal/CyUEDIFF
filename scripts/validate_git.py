"""Compare two real loose assets through disposable Git/LFS revisions.

Originals are read only. No checkout, remote, hook or source-repository change is
needed. The validation repository is created inside --output-dir and cleaned up.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--before', type=Path, required=True)
parser.add_argument('--after', type=Path, required=True)
parser.add_argument('--exe', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--consumer', type=Path, help='Optional separately built examples/git_consumer executable')
parser.add_argument('--output-dir', type=Path, default=Path('build-native/git-validation'))
args = parser.parse_args()
exe = args.exe.resolve(strict=True)
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
originals = [args.before.resolve(strict=True), args.after.resolve(strict=True)]
extensions = ['.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload']
sources = {p for asset in originals for p in [asset, *(asset.with_suffix(ext) for ext in extensions)] if p.is_file()}
checksums = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in sources}

def cli(*arguments, expected=0):
    r = subprocess.run([str(exe), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
    if r.returncode != expected:
        raise RuntimeError(f'Unexpected exit {r.returncode}: {r.stderr}')
    return json.loads(r.stdout)

baseline_process = subprocess.run([str(exe), 'diff', *map(str, originals)], capture_output=True, text=True, encoding='utf-8')
assert baseline_process.returncode in (0, 1), baseline_process.stderr
baseline = json.loads(baseline_process.stdout)
# TemporaryDirectory owns precisely this freshly created validation tree.
with tempfile.TemporaryDirectory(prefix='git-validation-', dir=out) as directory:
    base = Path(directory).resolve()
    assert base.parent == out
    repo = base / 'répo [local] avec espace'
    repo.mkdir()
    temporary = base / 'temporary'
    temporary.mkdir()
    cache = base / 'cache LFS externe' / 'objects'
    def git(*arguments):
        r = subprocess.run(['git', '-C', str(repo), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
        if r.returncode:
            raise RuntimeError(r.stderr)
        return r.stdout.strip()
    git('init', '--quiet')
    for key, value in [('user.name', 'CyUEDiff validation'), ('user.email', 'validation@invalid.example'),
                       ('core.autocrlf', 'false'), ('core.hooksPath', 'nonexistent-hooks'), ('commit.gpgSign', 'false')]:
        git('config', key, value)
    git('config', 'lfs.storage', cache.parent)
    target = repo / 'Content' / 'Asset [historique] é.uasset'
    target.parent.mkdir()
    def revision(asset, label):
        for path in [target, *(target.with_suffix(ext) for ext in extensions)]:
            if path.exists(): path.unlink()
        for source in [asset, *(asset.with_suffix(ext) for ext in extensions)]:
            if not source.exists(): continue
            destination = target if source == asset else target.with_suffix(source.name[len(asset.stem):])
            data = source.read_bytes()
            oid = hashlib.sha256(data).hexdigest()
            object_path = cache / oid[:2] / oid[2:4] / oid
            object_path.parent.mkdir(parents=True, exist_ok=True)
            object_path.write_bytes(data)
            destination.write_text(f'version https://git-lfs.github.com/spec/v1\noid sha256:{oid}\nsize {len(data)}\n', encoding='ascii', newline='\n')
        git('add', '--all')
        git('commit', '--quiet', '--allow-empty', '-m', label)
        return git('rev-parse', 'HEAD')
    before = revision(originals[0], 'before')
    after = revision(originals[1], 'after')
    path = target.relative_to(repo).as_posix()
    target.write_bytes(b'dirty checkout must remain untouched')
    protected = [target, repo / '.git' / 'index', repo / '.git' / 'HEAD', repo / '.git' / 'config']
    protected_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    inspection = cli('git-inspect', repo, before, path, '--git-temp', temporary)
    assert inspection['revision']['commit'] == before and inspection['revision']['present']
    assert inspection['revision']['files'] and all(f['storage'] == 'lfs-local' for f in inspection['revision']['files'])
    result = cli('git-diff', repo, before, after, path, '--git-temp', temporary, expected=baseline_process.returncode)
    assert result['comparison'] == {k: v for k, v in baseline.items() if k not in ('beforeDiagnostics', 'afterDiagnostics')}
    assert cli('git-diff', repo, before, before, path, '--git-temp', temporary)['status'] == 'identical'
    if args.consumer:
        consumer = args.consumer.resolve(strict=True)
        r = subprocess.run([str(consumer), str(repo), before, after, path], capture_output=True, text=True, encoding='utf-8')
        assert r.returncode == baseline_process.returncode and json.loads(r.stdout) == result, r.stderr

    after_oid = result['after']['files'][0]['lfsOid']
    object_path = cache / after_oid[:2] / after_oid[2:4] / after_oid
    # All refusal tests target copies in this disposable tree.
    refusals = [
        ('git-inspect', repo, before, path, '--out', target),
        ('git-inspect', repo, before, path, '--out', repo / '.git' / 'config'),
        ('git-inspect', repo, before, path, '--out', object_path),
        ('git-inspect', repo, before, '../Asset.uasset'),
        ('git-inspect', repo, before, path, '--metadata'),
        ('git-diff', repo, '--help', after, path),
    ]
    if os.name == 'nt':
        refusals.append(('git-inspect', repo, before, path, '--out', repo / '.GiT' / 'config'))
    for arguments in refusals:
        r = subprocess.run([str(exe), *map(str, arguments), '--git-temp', str(temporary)], capture_output=True, text=True, encoding='utf-8')
        assert r.returncode == 2 and 'error' in json.loads(r.stderr), (arguments, r.stdout, r.stderr)
    for p, sha in protected_hashes.items():
        assert hashlib.sha256(p.read_bytes()).hexdigest() == sha
    assert not list(temporary.iterdir()), 'Temporary revisions leaked'
    # Confirm missing LFS remains an actionable local-content error.
    missing_bytes = object_path.read_bytes()
    object_path.unlink()
    r = subprocess.run([str(exe), 'git-inspect', str(repo), after, path], capture_output=True, text=True, encoding='utf-8')
    assert r.returncode == 2 and json.loads(r.stderr)['error'] == 'lfs-missing'
    object_path.write_bytes(missing_bytes)
    result['repositoryUnchanged'] = True
    result['cliRefusals'] = len(refusals)
    result['localMissingLfsDiagnosed'] = True
    result['temporaryContentCleaned'] = True
    for destination, report in [(out / 'diff.json', result), (out / 'inspection.json', inspection)]:
        assert destination not in sources
        destination.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
for p, sha in checksums.items():
    assert hashlib.sha256(p.read_bytes()).hexdigest() == sha
print(json.dumps({'status': result['status'], 'changes': len(result['comparison']['changes']),
                  'materialChanges': len(result['comparison']['materialChanges']),
                  'cliRefusals': len(refusals), 'localMissingLfsDiagnosed': True,
                  'checkoutAndIndexUnchanged': True, 'originalsUnchanged': True,
                  'temporaryContentCleaned': True, 'installedConsumerVerified': bool(args.consumer)}, indent=2))
