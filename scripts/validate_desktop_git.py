"""Validate native desktop Git/LFS comparisons using copies of explicitly supplied local assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset', required=True, type=Path)
parser.add_argument('--after', required=True, type=Path)
parser.add_argument('--texture', type=Path)
parser.add_argument('--mesh', type=Path)
parser.add_argument('--codec', type=Path)
parser.add_argument('--exe', type=Path, default=Path('build-native/Release/cyuediff_desktop.exe'))
parser.add_argument('--cli', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/desktop-git-validation'))
args = parser.parse_args()
exe, cli = args.exe.resolve(strict=True), args.cli.resolve(strict=True)
extensions = ('.uasset', '.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload')
sources = [p.resolve(strict=True) for p in (args.asset, args.after, args.texture, args.mesh) if p]
inputs = [p for asset in sources for p in (asset, *(asset.with_suffix(e) for e in extensions[1:])) if p.is_file()]
if args.codec:
    inputs.append(args.codec.resolve(strict=True))
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as file:
        for block in iter(lambda: file.read(65536), b''):
            h.update(block)
    return h.hexdigest()

originals = {str(p): digest(p) for p in inputs}

def output(name):
    target = out / name
    if any(target == p or (target.exists() and target.samefile(p)) for p in (*inputs, exe, cli)):
        raise RuntimeError('Validation output overlaps an input')
    return target

# New owned repository on every invocation. It never has a remote or contacts a server.
repo = Path(tempfile.mkdtemp(prefix='repository-', dir=out)).resolve()
assert repo.parent == out and repo.name.startswith('repository-')
env = {k: v for k, v in os.environ.items() if not k.upper().startswith('GIT_')}
env['GIT_TERMINAL_PROMPT'] = '0'
reports = []
try:
    def git(*arguments):
        result = subprocess.run(['git', '-c', 'maintenance.auto=false', '-c', 'gc.auto=0', '-C', str(repo), *arguments],
                                env=env, capture_output=True, text=True, encoding='utf-8', timeout=45)
        if result.returncode:
            raise RuntimeError(f'Fixture Git command failed: {arguments[0]}: {result.stderr}')
        return result.stdout.strip()

    git('init', '--quiet')
    for key, value in [('user.name', 'CyUEDiff validation'), ('user.email', 'tests@invalid.example'),
                       ('commit.gpgSign', 'false'), ('core.hooksPath', 'nonexistent-hooks'),
                       ('core.autocrlf', 'false'), ('core.fsmonitor', 'false')]:
        git('config', key, value)
    objects = repo / '.git/lfs/objects'
    project = repo / 'Game.uproject'
    project.write_text('{"FileVersion":3,"EngineAssociation":"5.3"}', encoding='utf-8')

    def commit(message):
        git('add', '--all')
        git('commit', '--quiet', '-m', message)
        return git('rev-parse', 'HEAD')

    empty = commit('empty')

    def copy_asset(source, name):
        target = repo / 'Content' / name
        target.parent.mkdir(exist_ok=True)
        for index, extension in enumerate(extensions):
            original = source if index == 0 else source.with_suffix(extension)
            destination = target if index == 0 else target.with_suffix(extension)
            if not original.is_file():
                if destination.exists(): destination.unlink()
                continue
            oid = digest(original)
            cache = objects / oid[:2] / oid[2:4] / oid
            cache.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(original, cache)
            destination.write_bytes(f'version https://git-lfs.github.com/spec/v1\noid sha256:{oid}\nsize {original.stat().st_size}\n'.encode('utf-8'))

    path = 'Content/Asset é.uasset'
    copy_asset(args.asset.resolve(), 'Asset é.uasset')
    if args.texture: copy_asset(args.texture.resolve(), 'Texture.uasset')
    if args.mesh: copy_asset(args.mesh.resolve(), 'Mesh.uasset')
    before = commit('before')
    project.write_text('{"FileVersion":3,"EngineAssociation":"5.5"}', encoding='utf-8')
    context = commit('context')
    copy_asset(args.after.resolve(), 'Asset é.uasset')
    after = commit('after')
    index_before, checkout_before = digest(repo / '.git/index'), git('status', '--porcelain=v1')
    tracked_before = {p: digest(repo / p) for p in filter(None, git('ls-files', '-z').split('\0'))}

    def run(arguments, expected=0):
        result = subprocess.run([str(exe), '--smoke', '--repo', str(repo), *map(str, arguments)], env=env,
                                capture_output=True, text=True, encoding='utf-8', timeout=45)
        assert result.returncode == expected, (result.returncode, result.stderr, arguments)
        return result

    def capture(name, a=before, b=after, asset=path, view='comparison', locale='en', theme='mid-grey',
                project_context=False, expected=0, lfs_objects=None):
        image, report = output(name + '.png'), output(name + '.json')
        arguments = ['--before-revision', a, '--after-revision', b, '--before', asset,
                     '--view', view, '--locale', locale, '--theme', theme, '--capture', image, '--report', report]
        if project_context: arguments += ['--project', 'Game.uproject', '--no-engine']
        if lfs_objects: arguments += ['--lfs-objects', lfs_objects]
        if args.codec: arguments += ['--codec', args.codec.resolve()]
        run(arguments, expected)
        data = json.loads(report.read_text(encoding='utf-8'))
        assert data['frames'] >= 12 and data['drawVertices'] > 100 and data['renderer'] == 'Direct3D11 WARP'
        assert data['status'] == ('ready' if expected == 0 else 'failed')
        assert image.stat().st_size > 1000
        reports.append({'name': name, 'report': data})
        return data

    for theme in ('mid-grey', 'light-grey', 'amber-night', 'violet', 'navy'):
        for language in ('en', 'fr'):
            changed = capture(f'git-{theme}-{language}', theme=theme, locale=language)
            assert changed['document']['status'] == 'changed'
            assert changed['document']['git']['before']['commit'] == before
            assert changed['document']['git']['after']['commit'] == after
    direct = subprocess.run([str(cli), 'git-diff', str(repo), before, after, path], env=env,
                            capture_output=True, text=True, encoding='utf-8', timeout=45)
    assert direct.returncode == 1, direct.stderr
    assert json.loads(direct.stdout) == changed['document']['git'], 'Desktop/CLI Git diff mismatch'
    for language in ('en', 'fr'):
        addition = capture('added-' + language, a=empty, view='properties', locale=language)
        assert addition['document']['status'] == 'added' and addition['document']['before'] is None
        removal = capture('removed-' + language, b=empty, view='properties', locale=language)
        assert removal['document']['status'] == 'removed' and removal['document']['after'] is None
    addition_diff = capture('added-comparison', a=empty)
    removal_diff = capture('removed-comparison', b=empty, locale='fr')
    identical = capture('identical', b=before)
    assert identical['document']['status'] == 'identical'
    contextual = capture('context-fr', b=context, asset='/Game/Asset é', locale='fr', project_context=True)
    assert contextual['document']['status'] == 'changed' and contextual['document']['diff']['status'] == 'identical'
    assert contextual['document']['git']['contextChanges']
    if args.texture:
        texture = capture('texture-fr', asset='Content/Texture.uasset', view='textures', locale='fr')
        assert texture['uploadedPreviews'] == 2 and len(texture['document']['previews']) == 2
    if args.mesh:
        mesh = capture('mesh-en', asset='Content/Mesh.uasset', view='meshes')
        assert mesh['document']['beforeMesh']['triangles'] > 0 and mesh['document']['afterMesh']['triangles'] > 0
    external_cache = repo / 'external-lfs'
    objects.rename(external_cache)
    explicit = capture('explicit-lfs-en', lfs_objects=external_cache)
    assert explicit['document']['status'] == 'changed'
    external_cache.rename(objects)
    missing_oid = digest(args.after.resolve())
    missing_cache = objects / missing_oid[:2] / missing_oid[2:4] / missing_oid
    held = repo / 'held-lfs-object'
    missing_cache.rename(held)
    failed = capture('missing-lfs-fr', locale='fr', expected=2)
    assert failed['error'].startswith('lfs-missing:') and failed['document'] is None
    held.rename(missing_cache)
    refusals = 0
    for arguments in [ ['--before-revision', ''], ['--after-revision', ''],
                       ['--report', repo / '.git/protected.json'], ['--report', objects / 'protected.json'] ]:
        run(arguments, 2)
        refusals += 1
    # Both direct checkout assets and project descriptors are protected through hard links.
    for source, context_args in [(repo / path, []), (project, ['--project', 'Game.uproject', '--no-engine'])]:
        alias = output('protected-input.json')
        if alias.exists(): alias.unlink()
        os.link(source, alias)
        expected_hash = digest(source)
        run(['--before', path, '--before-revision', before, '--after-revision', after, '--report', alias, *context_args], 2)
        assert digest(source) == expected_hash
        alias.unlink()
        refusals += 1
    assert not (repo / '.git/protected.json').exists() and not (objects / 'protected.json').exists()
    assert index_before == digest(repo / '.git/index') and checkout_before == git('status', '--porcelain=v1')
    assert tracked_before == {p: digest(repo / p) for p in tracked_before}
    assert originals == {str(p): digest(p) for p in inputs}, 'Original assets changed'
    summary = {'schema': 'cyuediff.desktop-git-validation/1', 'captures': len(reports), 'refusals': refusals,
               'inputsUnchanged': True, 'checkoutUnchanged': True, 'reports': reports}
    output('validation.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f'{len(reports)} native Git/LFS captures, {refusals} refusals, CLI parity and unchanged checkout verified.')
finally:
    assert repo.resolve().parent == out and repo.name.startswith('repository-')
    def remove_readonly(function, name, error):
        if not isinstance(error[1], PermissionError):
            raise error[1]
        target = Path(name).resolve()
        assert target == repo or repo in target.parents
        os.chmod(target, stat.S_IWRITE)
        function(name)
    shutil.rmtree(repo, onerror=remove_readonly)
