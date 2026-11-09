"""Build the offline, dependency-free CyUEDiff GitHub Pages bundle."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import re
import shutil

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'site'
VERSION = re.search(r'project\(CyUEDiff VERSION (\d+\.\d+\.\d+)', (ROOT/'CMakeLists.txt').read_text(encoding='utf-8'))[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output', type=Path, default=ROOT / 'build-site/pages')
parser.add_argument('--include-local-build', action='store_true', help='Include the verified local Windows archive; never publish it automatically')
parser.add_argument('--local-build-record', type=Path, help='Verified local distribution record for --include-local-build')
args = parser.parse_args()
if args.local_build_record and not args.include_local_build:parser.error('--local-build-record requires --include-local-build')
out = args.output.resolve()
if not out.is_relative_to(ROOT) or out in (ROOT, SOURCE) or out.is_relative_to(SOURCE) or not out.relative_to(ROOT).parts[0].startswith('build'):
    parser.error('Use a generated build directory inside this workspace')
if out.exists() and any(out.iterdir()):
    if not (out / '.cyuediff-site-output').is_file():
        parser.error('Refusing to replace an unowned nonempty directory')
    # Resolved absolute target is verified above to remain below the workspace build directory.
    shutil.rmtree(out)
out.mkdir(parents=True, exist_ok=True)
(out / '.cyuediff-site-output').write_text('CyUEDiff generated static website\n', encoding='utf-8',newline='\n')
data = json.loads((SOURCE / 'content.json').read_text(encoding='utf-8-sig'))
for translations in data['translations'].values():
    for key, value in translations.items():
        translations[key] = value.replace('{version}', VERSION)
data['guide'] = json.loads((SOURCE / 'guide.json').read_text(encoding='utf-8-sig'))
assert data['translations']['en'].keys() == data['translations']['fr'].keys(), 'Translation key mismatch'
assert [s['id'] for s in data['guide']['en']] == [s['id'] for s in data['guide']['fr']]
shutil.copytree(SOURCE / 'assets', out / 'assets', dirs_exist_ok=True)

def digest(value): return hashlib.sha256(value).hexdigest()
def asset(name, value):
    path = Path('assets') / (Path(name).stem + '.' + digest(value)[:12] + Path(name).suffix)
    (out / path).write_bytes(value)
    return path.as_posix()

data['download'] = None
if args.include_local_build:
    record_path = args.local_build_record or ROOT/'build-native'/('distribution-'+VERSION+'.json')
    record = json.loads(record_path.read_text(encoding='utf-8'))
    assert record['version'] == VERSION and record.get('validated') is True, 'Use a verified archive for this source version'
    archive = ROOT / 'build-native' / Path(record['archive']).name
    assert digest(archive.read_bytes()) == record['sha256'], 'Local archive has changed; validate it again'
    destination = out / 'downloads' / archive.name
    destination.parent.mkdir(exist_ok=True)
    shutil.copyfile(archive, destination)
    data['download'] = {'href': 'downloads/' + archive.name, 'filename': archive.name, 'sha256': record['sha256'], 'bytes': archive.stat().st_size, 'localPreviewOnly': True}

values = {
    'version': VERSION,
    'styles': asset('styles.css', (SOURCE / 'styles.css').read_bytes()),
    'app': asset('app.js', (SOURCE / 'app.js').read_bytes()),
    'content': asset('content.js', ('window.CYUE_SITE = ' + json.dumps(data, ensure_ascii=False, separators=(',', ':')) + ';\n').encode('utf-8')),
}
for name in ('index.html', 'guide.html'):
    template = (SOURCE / 'templates' / name).read_text(encoding='utf-8')
    if name == 'guide.html':
        template = template.replace('href="#review"', 'href="index.html#review" data-feature-link')
        template = template.replace('href="#download"', 'href="index.html#download" data-main-download')
    guide = data['guide']['en']
    body = ''.join(f'<section id="{s["id"]}"><h2>{html.escape(s["title"])}</h2>{s["html"]}</section>' for s in guide)
    template = template.replace('{{guide}}', body)
    template = re.sub(r'\{\{t:([^}]+)\}\}', lambda m: html.escape(data['translations']['en'][m[1]], quote=True), template)
    for key, value in values.items(): template = template.replace('{{' + key + '}}', value)
    assert '{{' not in template, name
    (out / name).write_text(template, encoding='utf-8', newline='\n')
(out / '.nojekyll').write_text('', encoding='utf-8',newline='\n')
(out / 'asset-manifest.json').write_text(json.dumps({'version': VERSION, 'assets': {k:v for k,v in values.items() if k != 'version'}, 'download': data['download']}, indent=2) + '\n', encoding='utf-8',newline='\n')
print('Static site ready:', out)
print('Style/script URLs use content hashes. No hosting or publication was performed.')
