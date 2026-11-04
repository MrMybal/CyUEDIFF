"""Validate historical project contexts using copies of a real asset and project.

Creates and removes its own Git repository, LFS cache and two small engine fixtures.
No source checkout, project descriptor, plugin or installed engine is modified.
"""
import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset', type=Path, required=True)
parser.add_argument('--project', type=Path, required=True)
parser.add_argument('--real-engine-root', type=Path)
parser.add_argument('--consumer', type=Path)
parser.add_argument('--exe', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/git-project-validation'))
args = parser.parse_args()
exe = args.exe.resolve(strict=True)
asset = args.asset.resolve(strict=True)
project = args.project.resolve(strict=True)
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
extensions = ['.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload']
sources = [asset, *(asset.with_suffix(ext) for ext in extensions)]
sources = [p for p in sources if p.is_file()]
plugins = list((project.parent / 'Plugins').rglob('*.uplugin')) if (project.parent / 'Plugins').is_dir() else []
originals = [project, *sources, *plugins]
checksums = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
# The local sample descriptor is standard JSON. The native reader also accepts JSON comments.
source_descriptor = json.loads(project.read_text(encoding='utf-8-sig'))

def cli(*arguments, expected=0):
    r = subprocess.run([str(exe), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
    if r.returncode != expected:
        raise RuntimeError(f'Unexpected exit {r.returncode}: {r.stderr}')
    return json.loads(r.stdout)

with tempfile.TemporaryDirectory(prefix='project-validation-', dir=out) as directory:
    base = Path(directory).resolve()
    assert base.parent == out
    repo = base / 'répo historique'
    repo.mkdir()
    temporary = base / 'temporary'
    temporary.mkdir()
    def git(*arguments):
        r = subprocess.run(['git', '-C', str(repo), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
        if r.returncode:
            raise RuntimeError(r.stderr)
        return r.stdout.strip()
    git('init', '--quiet')
    for key, value in [('user.name', 'CyUEDiff validation'), ('user.email', 'validation@invalid.example'),
                       ('core.hooksPath', 'nonexistent-hooks'), ('commit.gpgSign', 'false'), ('core.autocrlf', 'false')]:
        git('config', key, value)
    cache = repo / '.git' / 'lfs' / 'objects'
    def put(file, data, lfs=False):
        file.parent.mkdir(parents=True, exist_ok=True)
        if lfs:
            oid = hashlib.sha256(data).hexdigest()
            object_path = cache / oid[:2] / oid[2:4] / oid
            object_path.parent.mkdir(parents=True, exist_ok=True)
            object_path.write_bytes(data)
            data = f'version https://git-lfs.github.com/spec/v1\noid sha256:{oid}\nsize {len(data)}\n'.encode('ascii')
        file.write_bytes(data)
    def package(target, lfs=False):
        for source in sources:
            destination = target if source == asset else target.with_suffix(source.name[len(asset.stem):])
            put(destination, source.read_bytes(), lfs)
    project_path = 'Game/' + project.name
    project_file = repo / project_path
    plugin_file = repo / 'Game/Plugins/ValidationContent/ValidationContent.uplugin'
    put(plugin_file, b'{"CanContainContent":true}', True)
    for plugin in plugins:
        put(repo / 'Game' / plugin.relative_to(project.parent), plugin.read_bytes(), True)
    package(repo / 'Game/Content/Test.uasset', True)
    package(repo / 'Game/Plugins/ValidationContent/Content/Test.uasset', True)
    descriptor = copy.deepcopy(source_descriptor)
    descriptor['EngineAssociation'] = '5.3'
    descriptor['Plugins'] = [p for p in descriptor.get('Plugins', []) if p['Name'] != 'ValidationContent'] + [{'Name': 'ValidationContent', 'Enabled': True}]
    put(project_file, json.dumps(descriptor).encode('utf-8'), True)
    git('add', '--all')
    git('commit', '--quiet', '-m', 'before context')
    before = git('rev-parse', 'HEAD')
    descriptor['EngineAssociation'] = '5.5'
    descriptor['Plugins'][-1]['Enabled'] = False
    put(project_file, json.dumps(descriptor, indent=2).encode('utf-8'), True)
    git('add', '--all')
    git('commit', '--quiet', '-m', 'after context only')
    after = git('rev-parse', 'HEAD')
    engines = []
    for minor in (3, 5):
        engine = base / f'Engine 5.{minor}'
        put(engine / 'Engine/Build/Build.version', json.dumps({'MajorVersion': 5, 'MinorVersion': minor, 'PatchVersion': 2}).encode('ascii'))
        package(engine / 'Engine/Content/Test.uasset')
        engines.append(engine)
    protected = [project_file, plugin_file, repo / 'Game/Content/Test.uasset', repo / '.git/index', repo / '.git/HEAD', repo / '.git/config']
    project_file.write_bytes(b'dirty project descriptor')
    (repo / 'Game/Content/Test.uasset').write_bytes(b'dirty asset')
    protected_hashes = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in protected}
    common = ['--project', project_path, '--git-temp', temporary]
    before_context = cli('git-project', repo, before, project_path, '--engine-root', engines[0], '--git-temp', temporary)
    after_context = cli('git-project', repo, after, project_path, '--engine-root', engines[1], '--git-temp', temporary)
    assert before_context['complete'] and after_context['complete']
    assert before_context['engineAssociation'] == '5.3' and after_context['engineAssociation'] == '5.5'
    assert '/ValidationContent' in before_context['mounts'] and '/ValidationContent' not in after_context['mounts']
    inspection = cli('git-inspect', repo, before, '/ValidationContent/Test', *common, '--engine-root', engines[0])
    assert inspection['revision']['path'] == 'Game/Plugins/ValidationContent/Content/Test.uasset'
    result = cli('git-diff', repo, before, after, '/Game/Test', *common, '--before-engine-root', engines[0], '--after-engine-root', engines[1], expected=1)
    assert result['status'] == 'changed' and result['comparison']['status'] == 'identical'
    assert result['contextChanges'] and any(change['path'] == '/engineAssociation' for change in result['contextChanges'])
    assert not result['comparison']['changes']
    external = cli('git-inspect', repo, before, '/Engine/Test', *common, '--engine-root', engines[0])
    assert external['revision']['storage'] == 'engine-local'
    assert all(file['storage'] == 'engine-local' and file['sha256'] for file in external['revision']['files'])
    descriptor_only = cli('git-project', repo, before, project_path, '--no-engine')
    assert descriptor_only['complete'] and not descriptor_only['engine']['root']
    assert descriptor_only['pluginActivationEvaluated'] is False
    if args.consumer:
        r = subprocess.run([str(args.consumer.resolve(strict=True)), str(repo), before, after, '/Game/Test', project_path, project_path, *map(str, engines)], capture_output=True, text=True, encoding='utf-8')
        assert r.returncode == 1 and json.loads(r.stdout) == result, r.stderr
    refusals = [
        ('git-inspect', repo, before, '/Game/Test', *common, '--no-engine', '--out', project_file),
        ('git-inspect', repo, before, '/Game/Test', *common, '--no-engine', '--out', plugin_file),
        ('git-inspect', repo, before, '/Engine/Test', *common, '--engine-root', engines[0], '--out', engines[0] / 'Engine/Build/Build.version'),
        ('git-inspect', repo, before, '/Engine/Test', *common, '--engine-root', engines[0], '--out', engines[0] / 'Engine/Content/Test.uasset'),
        ('git-inspect', repo, before, '/Game/../Test', *common, '--no-engine'),
        ('git-diff', repo, before, after, '/Game/Test', '--before-project', project_path),
        ('git-diff', repo, before, after, 'Game/Content/Test.uasset', '--engine-root', engines[0]),
        ('git-project', repo, before, project_path, '--no-engine', '--engine-root', engines[0]),
        ('git-project', repo, before, '../Outside.uproject', '--no-engine'),
        ('git-inspect', repo, after, '/ValidationContent/Test', *common, '--no-engine'),
    ]
    for arguments in refusals:
        r = subprocess.run([str(exe), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
        assert r.returncode == 2 and 'error' in json.loads(r.stderr), (arguments, r.stdout, r.stderr)
    real = None
    if args.real_engine_root:
        r = subprocess.run([str(exe), 'git-project', str(repo), before, project_path, '--engine-root', str(args.real_engine_root.resolve(strict=True))], capture_output=True, text=True, encoding='utf-8')
        assert r.returncode in (0, 2) and r.stdout, r.stderr
        real = json.loads(r.stdout)
        assert real['engine']['major'] == 5 and '/Engine' in real['mounts']
        (out / 'installed-engine-context.json').write_text(json.dumps(real, indent=2, ensure_ascii=False), encoding='utf-8')
        if not real['complete']:
            print(json.dumps({'realEngineDiagnostics': real['diagnostics']}, ensure_ascii=False))
    for p, sha in protected_hashes.items():
        assert hashlib.sha256(p.read_bytes()).hexdigest() == sha
    assert not list(temporary.iterdir())
    for name, report in [('before-context.json', before_context), ('after-context.json', after_context), ('context-only-diff.json', result), ('engine-inspection.json', external)]:
        destination = out / name
        assert destination not in originals
        destination.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
for p, sha in checksums.items():
    assert hashlib.sha256(p.read_bytes()).hexdigest() == sha
summary = {'contextOnlyChangeDetected': True, 'assetComparison': result['comparison']['status'],
           'beforeEngine': '5.3', 'afterEngine': '5.5', 'disabledPluginExcluded': True,
           'localEngineProvenanceRecorded': True, 'cliRefusals': len(refusals), 'originalsUnchanged': True,
           'checkoutAndIndexUnchanged': True, 'temporaryContentCleaned': True, 'installedConsumerVerified': bool(args.consumer), 'realEngineCatalogueComplete': real['complete'] if real else None}
(out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
