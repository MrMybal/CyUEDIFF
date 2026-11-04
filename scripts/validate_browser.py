"""Validate native project/Git content browsing on copies of explicitly provided assets."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset', required=True, type=Path)
parser.add_argument('--after', required=True, type=Path)
parser.add_argument('--exe', type=Path, default=Path('build-native/Release/cyuediff_desktop.exe'))
parser.add_argument('--cli', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/browser-validation'))
args = parser.parse_args()
exe, cli = args.exe.resolve(strict=True), args.cli.resolve(strict=True)
extensions = ('.uasset', '.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload')
originals = [p for source in (args.asset.resolve(strict=True), args.after.resolve(strict=True))
             for p in (source, *(source.with_suffix(e) for e in extensions[1:])) if p.is_file()]

def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(65536), b''): h.update(block)
    return h.hexdigest()

original_hashes = {str(p): digest(p) for p in originals}
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)
owned = Path(tempfile.mkdtemp(prefix='fixture-', dir=out)).resolve()
assert owned.parent == out and owned.name.startswith('fixture-')
repo, engine = owned / 'project', owned / 'engine'
repo.mkdir()
env = {k: v for k, v in os.environ.items() if not k.upper().startswith('GIT_')}
env['GIT_TERMINAL_PROMPT'] = '0'
reports = []
try:
    def git(*arguments):
        result = subprocess.run(['git', '-c', 'maintenance.auto=false', '-c', 'gc.auto=0', '-C', str(repo), *arguments],
                                env=env, capture_output=True, text=True, encoding='utf-8', timeout=45)
        assert result.returncode == 0, result.stderr
        return result.stdout.strip()

    git('init', '--quiet')
    for key, value in [('user.name','CyUEDiff validation'), ('user.email','tests@invalid.example'),
                       ('commit.gpgSign','false'), ('core.hooksPath','no-hooks'),
                       ('core.autocrlf','false'), ('core.fsmonitor','false')]: git('config', key, value)
    project = repo / 'Game.uproject'
    project.write_text('{"FileVersion":3,"EngineAssociation":"5.5","Plugins":[{"Name":"Disabled","Enabled":false}]}', encoding='utf-8')
    main_path = 'Content/Blueprints/TEST.uasset'
    objects = repo / '.git/lfs/objects'

    def copy(source, pointers):
        source = source.resolve()
        target = repo / main_path
        target.parent.mkdir(parents=True, exist_ok=True)
        for index, extension in enumerate(extensions):
            old = source if index == 0 else source.with_suffix(extension)
            new = target if index == 0 else target.with_suffix(extension)
            if not old.is_file():
                if new.exists(): new.unlink()
                continue
            if pointers:
                oid = digest(old)
                cached = objects / oid[:2] / oid[2:4] / oid
                cached.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(old, cached)
                new.write_bytes(f'version https://git-lfs.github.com/spec/v1\noid sha256:{oid}\nsize {old.stat().st_size}\n'.encode('utf-8'))
            else: shutil.copyfile(old, new)

    for name in ['Content/Maps/World.umap','Content/Data/Metadata.uasset','Content/BeforeOnly.uasset',
                 'Plugins/Fx/Content/Material.uasset','Plugins/Disabled/Content/Hidden.uasset']:
        path = repo / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'')
    for name in ('Fx', 'Disabled'):
        (repo / 'Plugins' / name / (name + '.uplugin')).write_text('{"CanContainContent":true}', encoding='utf-8')
    (engine / 'Engine/Build').mkdir(parents=True)
    (engine / 'Engine/Build/Build.version').write_text('{"MajorVersion":5,"MinorVersion":5,"PatchVersion":4}', encoding='utf-8')
    (engine / 'Engine/Content').mkdir()
    (engine / 'Engine/Content/Base.uasset').write_bytes(b'')
    def commit(message):
        git('add', '--all'); git('commit', '--quiet', '-m', message); return git('rev-parse', 'HEAD')
    copy(args.asset, True)
    before = commit('before')
    (repo / 'Content/BeforeOnly.uasset').unlink()
    (repo / 'Content/AfterOnly.uasset').write_bytes(b'')
    copy(args.after, True)
    after = commit('after')
    # A deliberately dirty checkout proves Git browsing never depends on its asset bytes.
    copy(args.after, False)
    index_before = digest(repo / '.git/index')
    checkout_before = git('status', '--porcelain=v1')
    tracked = {p: digest(repo / p) for p in filter(None, git('ls-files','-z').split('\0'))}

    def output(name):
        path = out / name
        if any(path == p or (path.exists() and path.samefile(p)) for p in (*originals, exe, cli)):
            raise RuntimeError('Output overlaps original input')
        return path
    def run(arguments, expected=0):
        result = subprocess.run([str(exe), '--smoke', *map(str, arguments)], env=env, capture_output=True,
                                text=True, encoding='utf-8', timeout=45)
        assert result.returncode == expected, (result.returncode, result.stderr, arguments)
    local = ['--project', project, '--engine-root', engine]
    historical = ['--repo', repo, '--before-revision', before, '--after-revision', after]
    def capture(name, arguments, locale='en', theme='mid-grey', expected=0):
        image, report = output(name + '.png'), output(name + '.json')
        run(['--browse', '--locale', locale, '--theme', theme, '--capture', image, '--report', report, *arguments], expected)
        data = json.loads(report.read_text(encoding='utf-8'))
        assert data['status'] == ('ready' if expected == 0 else 'failed') and data['frames'] >= 12
        assert data['renderer'] == 'Direct3D11 WARP' and data['drawVertices'] > 100
        with Image.open(image) as pixels:
            assert pixels.width >= 960 and any(a != b for a,b in pixels.convert('RGB').getextrema())
        reports.append({'name':name, 'report':data})
        return data
    partial = subprocess.run([str(cli),'catalog',str(project),'--engine-root',str(engine),'--max-assets','1'],
                             env=env,capture_output=True,text=True,encoding='utf-8',timeout=45)
    assert partial.returncode == 2 and not json.loads(partial.stdout)['complete']
    refused = subprocess.run([str(cli),'catalog',str(project),'--engine-root',str(engine),'--max-assets','1',
                              '--out',str(repo / 'Content/AfterOnly.uasset')],
                             env=env,capture_output=True,text=True,encoding='utf-8',timeout=45)
    assert refused.returncode == 2 and json.loads(refused.stderr)['error'] == 'output-path'
    for theme in ('mid-grey','light-grey','amber-night','violet','navy'):
        for language in ('en','fr'):
            catalog = capture('local-' + theme + '-' + language, local, language, theme)
            assert len(catalog['browser']['assets']) == 5 and catalog['browser']['visiblePackages'] == 5
            assert all(not row['label'].startswith(('/Engine','/Disabled')) for row in catalog['browser']['assets'])
    assert len({digest(output('local-' + theme + '-en.png')) for theme in ('mid-grey','light-grey','amber-night','violet','navy')}) == 5
    direct = subprocess.run([str(cli), 'catalog', str(project), '--engine-root', str(engine)], env=env,
                            capture_output=True, text=True, encoding='utf-8', timeout=45)
    assert direct.returncode == 0 and json.loads(direct.stdout) == catalog['browser']['before']
    for language in ('en','fr'):
        catalog = capture('git-' + language, historical, language)
        assert len(catalog['browser']['assets']) == 7
        rows = {row['path']:row for row in catalog['browser']['assets']}
        assert rows['Content/BeforeOnly.uasset']['before'] and not rows['Content/BeforeOnly.uasset']['after']
        assert rows['Content/AfterOnly.uasset']['after'] and not rows['Content/AfterOnly.uasset']['before']
    direct = subprocess.run([str(cli),'git-catalog',str(repo),before], env=env, capture_output=True,
                            text=True, encoding='utf-8', timeout=45)
    assert direct.returncode == 0 and json.loads(direct.stdout) == catalog['browser']['before']
    filtered = capture('filter-fr', [*local,'--browse-filter','test'], 'fr')
    assert filtered['browser']['visiblePackages'] == 1
    empty = capture('no-matches', [*local,'--browse-filter','nothing-matches'])
    assert empty['browser']['visiblePackages'] == 0
    included = capture('include-engine', [*local,'--include-engine'])
    assert len(included['browser']['assets']) == 6
    selected = capture('open-local-fr', [*local,'--browse-select','/Game/Blueprints/TEST'], 'fr')
    assert selected['document']['before']['completeInput'] and not selected['browser']['open']
    compared = capture('open-git-en', [*historical,'--browse-select',main_path,'--view','comparison'])
    assert compared['document']['status'] == 'changed' and compared['document']['git']['before']['commit'] == before
    missing_oid = digest(args.after.resolve())
    cached = objects / missing_oid[:2] / missing_oid[2:4] / missing_oid
    held = owned / 'held-lfs'; cached.rename(held)
    missing = capture('missing-lfs-catalog', historical)
    assert missing['browser']['complete'] and len(missing['browser']['assets']) == 7
    held.rename(cached)
    capture('missing-project-fr', ['--project', owned / 'missing.uproject'], 'fr', expected=2)
    capture('bad-selection', [*local,'--browse-select','/Game/Missing'], expected=2)
    capture('bad-git-ref', ['--repo',repo,'--before-revision','not-a-commit'], expected=2)
    refusals=1
    for protected in [repo / 'Content/protected.json', repo / '.git/protected.json', engine / 'Engine/protected.json']:
        run(['--browse', *local, '--report', protected], 2) if protected != repo / '.git/protected.json' else run(['--browse',*historical,'--report',protected],2)
        assert not protected.exists()
        refusals += 1
    alias = output('asset-alias.json')
    if alias.exists(): alias.unlink()
    os.link(repo / main_path, alias)
    run(['--browse', *local, '--report', alias], 2)
    alias.unlink(); refusals += 1
    assert index_before == digest(repo / '.git/index') and checkout_before == git('status','--porcelain=v1')
    assert tracked == {p:digest(repo / p) for p in tracked}
    assert original_hashes == {str(p):digest(p) for p in originals}
    assert len(reports) == 21
    output('validation.json').write_text(json.dumps({'schema':'cyuediff.browser-validation/1','captures':len(reports),
        'refusals':refusals,'inputsUnchanged':True,'checkoutUnchanged':True,'reports':reports},indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    print(f'{len(reports)} native browser captures, {refusals} output refusals, CLI parity and unchanged inputs verified.')
finally:
    assert owned.resolve().parent == out and owned.name.startswith('fixture-')
    def remove_readonly(function, name, error):
        if not isinstance(error[1], PermissionError): raise error[1]
        path = Path(name).resolve(); assert path == owned or owned in path.parents
        os.chmod(path,stat.S_IWRITE); function(name)
    shutil.rmtree(owned,onerror=remove_readonly)
