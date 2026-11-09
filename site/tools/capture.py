"""Create public screenshots from original test fixtures, never from user projects."""
import argparse
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
VERSION = re.search(r'project\(CyUEDiff VERSION (\d+\.\d+\.\d+)',(ROOT/'CMakeLists.txt').read_text(encoding='utf-8'))[1]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build', type=Path, default=ROOT / 'build-native')
p.add_argument('--refresh-views', action='store_true')
a = p.parse_args()
binary = a.build.resolve() / 'Release'
work = ROOT / 'build-site/captures'
samples = work / 'samples'
assets = ROOT / 'site/assets/screenshots'
samples.mkdir(parents=True, exist_ok=True)
assets.mkdir(parents=True, exist_ok=True)
env = {k.upper(): v for k, v in os.environ.items() if not k.upper().startswith('GIT_')}
env['LOCALAPPDATA'] = str(work / 'unused-profile')

def run(args, **kwargs):
    result = subprocess.run(list(map(str, args)), check=False, capture_output=True, env=env, timeout=180, **kwargs)
    if result.returncode:
        raise RuntimeError(result.stderr.decode('utf-8', 'replace'))
    return result.stdout.decode('utf-8', 'replace')

assert run([binary/'cyuediff.exe','--version']).strip()==VERSION, 'Build the current source version before capturing'

views = work / 'views'
views_version=work/'views-version.txt'
if a.refresh_views or not (views/'report.json').exists() or not views_version.is_file() or views_version.read_text(encoding='utf-8').strip()!=VERSION:
    run([binary/'cyuediff_visual_navigation_tests.exe','--capture-dir',views],cwd=ROOT)
    views_version.write_text(VERSION+'\n',encoding='utf-8',newline='\n')
for name in ('rich_curve_eval', 'mesh_morph', 'mesh_skin', 'workspace', 'collision', 'schema_native'):
    run([binary / ('cyuediff_' + name + '_tests.exe'), '--write-fixtures', samples], cwd=ROOT)
cli = binary / 'cyuediff.exe'
desktop = binary / 'cyuediff_desktop.exe'
curve = json.loads(run([cli, 'curves', samples / 'curve-0-1013.uasset']))
owner = next(iter(curve['owners']))
middle = curve['owners'][owner]['lanes']['value']['keyOrder'][1]

repo = work / 'revisions'
repo.mkdir(exist_ok=True)
marker = repo / '.site-demo-owned'
if (repo / '.git').exists() and not marker.exists():
    raise RuntimeError('Refusing an existing unowned repository')
def git(*args):
    return run(['git', '-c', 'safe.directory=' + repo.as_posix(), '-C', repo, *args]).strip()
if not (repo / '.git').exists():
    marker.write_text('Original CyUEDiff website demo repository\n', encoding='utf-8',newline='\n')
    git('init', '-q', '-b', 'codex/site-demo')
    for key, value in [('user.name', 'Demo'), ('user.email', 'demo@invalid.example'), ('commit.gpgSign', 'false'), ('core.hooksPath', 'missing-hooks')]:
        git('config', key, value)
    (repo / 'Content').mkdir(exist_ok=True)
    target = repo / 'Content/DA_Interaction.uasset'
    for source, message in [('Before.uasset', 'Add interaction settings'), ('After.uasset', 'Adjust interaction distance')]:
        shutil.copyfile(samples / source, target)
        git('add', 'Content')
        git('commit', '-q', '-m', message)
    git('commit', '-q', '--allow-empty', '-m', 'Review source changes')

records = []
def capture(name, locale, args, state=None):
    destination = assets / (name + '-' + locale + '.png')
    command = [desktop, '--smoke', '--no-preferences', '--locale', locale, '--capture', destination, '--report', work / (name + '-' + locale + '.json'), *args]
    if state:
        command += ['--document-action', 'view-state:' + json.dumps(state, separators=(',', ':'))]
    run(command, cwd=work)
    records.append({'file': destination.name, 'version': VERSION, 'origin': 'Windows desktop with original encoded test fixtures', 'sha256': hashlib.sha256(destination.read_bytes()).hexdigest()})

for locale in ('en', 'fr'):
    capture('overview', locale, ['--before', 'samples/curve-0-1013.uasset', '--after', 'samples/curve-1-1013.uasset', '--view', 'curves'], {'asset': {'curves': {'time': .5, 'inspection': {'selected': middle, 'changedOnly': True, 'properties': {'changedOnly': True, 'selected': '/value'}}}}})
    capture('morphs', locale, ['--before', 'samples/morph-before.uasset', '--after', 'samples/morph-after.uasset', '--view', 'meshes'], {'asset': {'meshMorphInspection': True, 'meshMorphs': {'filter': 'Smile', 'selected': '/targets/Smile/positions/0/0', 'changedOnly': True}}})
    capture('properties', locale, ['--before', 'samples/Before.uasset', '--after', 'samples/After.uasset', '--view', 'properties'])
    capture('mesh', locale, ['--before', 'samples/morph-before.uasset', '--after', 'samples/morph-after.uasset', '--view', 'meshes'])
    capture('containers', locale, ['--container', 'samples/native.utoc', '--container-entry', '000000000000000000000001', '--cooked-profile', 'ue5.3', '--script-objects', 'samples/global.utoc', '--schema', 'samples/native.json', '--cooked-view', 'properties', '--after-container', 'samples/changed.utoc', '--after-schema', 'samples/native.json', '--after-cooked-profile', 'ue5.3', '--after-script-objects', 'samples/global.utoc'])
    capture('history', locale, ['--repo', 'revisions', '--history'])
    capture('comparison', locale, ['--before', 'samples/Before.uasset', '--after', 'samples/After.uasset', '--view', 'comparison'])
    for scene, output in [('blueprint', 'blueprint'), ('material', 'materials'), ('parameters', 'parameters'), ('texture', 'textures'), ('collision-all', 'collisions'), ('reference-pose', 'skeleton'), ('source-keys', 'animation'), ('curve-weighted', 'curves'), ('pose-hierarchy', 'pose')]:
        source = views / ('mid-grey-' + locale + '-' + scene + '.png')
        destination = assets / (output + '-' + locale + '.png')
        shutil.copyfile(source, destination)
        records.append({'file': destination.name, 'version': VERSION, 'origin': 'Actual CyUI component, original in-memory demo snapshots', 'scene': scene, 'sha256': hashlib.sha256(destination.read_bytes()).hexdigest()})
(assets.parent / 'capture-manifest.json').write_text(json.dumps({'version': VERSION, 'privateProjectData': False, 'records': records}, indent=2) + '\n', encoding='utf-8',newline='\n')
subprocess.run([os.sys.executable,str(ROOT/'site/tools/capture_cooked.py'),'--build',str(a.build.resolve())],check=True,env=env)
print(f'Prepared {len(records)+2} public original-fixture captures')
