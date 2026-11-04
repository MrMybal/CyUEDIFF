"""Exercise the native DX11/WARP desktop host on explicit local assets, without opening visible windows."""
import argparse
import hashlib
import json
import os
import shutil
from pathlib import Path
import subprocess
from PIL import Image

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset', required=True, type=Path, help='Blueprint or other loose package')
parser.add_argument('--after', type=Path, help='Optional second version of --asset')
parser.add_argument('--texture', type=Path)
parser.add_argument('--mesh', type=Path)
parser.add_argument('--material', type=Path)
parser.add_argument('--codec', type=Path)
parser.add_argument('--exe', type=Path, default=Path('build-native/Release/cyuediff_desktop.exe'))
parser.add_argument('--cli', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/desktop-validation'))
args = parser.parse_args()
exe, cli = args.exe.resolve(strict=True), args.cli.resolve(strict=True)
assets = [p.resolve(strict=True) for p in [args.asset, args.after, args.texture, args.mesh, args.material] if p]
inputs = [p for asset in assets for p in [asset, *[asset.with_suffix(e) for e in ('.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload')]] if p.is_file()]
if args.codec:
    inputs.append(args.codec.resolve(strict=True))
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

def fingerprint():
    return {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in inputs}
originals = fingerprint()

def output(name):
    target = out / name
    if any(target == p or (target.exists() and target.samefile(p)) for p in [*inputs, exe, cli]):
        raise RuntimeError('Test output overlaps input')
    return target

def run(arguments, expected=0):
    result = subprocess.run([str(exe), '--smoke', *map(str, arguments)], capture_output=True,
                            text=True, encoding='utf-8', timeout=45)
    assert result.returncode == expected, (result.returncode, result.stderr, arguments)
    return result

reports = []
def capture(name, asset=None, view='properties', locale='en', theme='mid-grey', after=None, expected=0, codec=False):
    image, report = output(name + '.png'), output(name + '.json')
    arguments = ['--capture', image, '--report', report, '--view', view, '--locale', locale, '--theme', theme]
    if asset: arguments += ['--before', asset]
    if after: arguments += ['--after', after]
    if codec and args.codec: arguments += ['--codec', args.codec]
    run(arguments, expected)
    data = json.loads(report.read_text(encoding='utf-8'))
    assert data['status'] == ('ready' if expected == 0 else 'failed')
    assert data['frames'] >= 12 and data['drawVertices'] > 100 and data['renderer'] == 'Direct3D11 WARP'
    assert data['locale'] == locale and data['theme'] == theme and data['view'] == view
    with Image.open(image) as pixels:
        assert pixels.width >= 960 and pixels.height >= 640
        assert any(low != high for low, high in pixels.convert('RGB').getextrema()), 'Blank screenshot'
    reports.append({'name': name, 'report': data})
    return data

for theme in ('mid-grey', 'light-grey', 'amber-night', 'violet', 'navy'):
    for locale in ('en', 'fr'):
        capture(f'blueprint-{theme}-{locale}', args.asset, 'blueprint', locale, theme)
assert len({hashlib.sha256((out / f'blueprint-{theme}-en.png').read_bytes()).hexdigest()
            for theme in ('mid-grey', 'light-grey', 'amber-night', 'violet', 'navy')}) == 5
unicode_asset = output('Lecture [é].uasset')
shutil.copyfile(args.asset, unicode_asset)
for extension in ('.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload'):
    source = args.asset.with_suffix(extension)
    if source.is_file(): shutil.copyfile(source, output('Lecture [é]' + extension))
unicode_report = capture('unicode-fr', unicode_asset, locale='fr')
assert Path(unicode_report['document']['before']['source']).name == unicode_asset.name
capture('properties-fr', args.asset, locale='fr')
capture('empty-en')
failed = capture('missing-fr', output('missing.uasset'), locale='fr', expected=2)
assert failed['error'] and failed['document'] is None
if args.after:
    comparison = capture('comparison-fr', args.asset, 'comparison', 'fr', after=args.after)
    direct = subprocess.run([str(cli), 'diff', str(args.asset), str(args.after)], capture_output=True,
                            text=True, encoding='utf-8', timeout=45)
    assert direct.returncode in (0, 1, 2)
    result = json.loads(direct.stdout)
    assert result.pop('beforeDiagnostics') == comparison['document']['before']['diagnostics']
    assert result.pop('afterDiagnostics') == comparison['document']['after']['diagnostics']
    assert comparison['document']['diff'] == result, 'Desktop/CLI diff mismatch'
if args.texture:
    for locale in ('en', 'fr'):
        texture = capture('texture-' + locale, args.texture, 'textures', locale, codec=True)
        assert texture['uploadedPreviews'] == 1 and texture['document']['previews'][0]['width'] > 0
if args.mesh:
    for locale in ('en', 'fr'):
        mesh = capture('mesh-' + locale, args.mesh, 'meshes', locale, codec=True)
        assert mesh['document']['beforeMesh']['triangles'] > 0
if args.material:
    for locale in ('en', 'fr'):
        capture('material-' + locale, args.material, 'materials', locale)

for arguments in [ ['--view', 'unknown'], ['--locale', 'unknown'], ['--theme', 'unknown'],
                   ['--capture', output('invalid.uasset')], ['--after', args.asset], ['--unknown'] ]:
    run(arguments, 2)
# A report name hard-linked to an asset must be rejected without truncating the asset.
protected = output('protected.uasset')
protected.write_bytes(args.asset.read_bytes())
alias = output('protected.json')
if alias.exists(): alias.unlink()
os.link(protected, alias)
protected_hash = hashlib.sha256(protected.read_bytes()).hexdigest()
run(['--before', protected, '--report', alias], 2)
assert protected_hash == hashlib.sha256(protected.read_bytes()).hexdigest()
alias.unlink(); protected.unlink()
assert originals == fingerprint(), 'Source inputs changed'
summary = {'schema': 'cyuediff.desktop-validation/1', 'captures': len(reports), 'refusals': 7,
           'inputsUnchanged': True, 'reports': reports}
output('validation.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
print(f'{len(reports)} native captures, 7 refusals, desktop/CLI diff parity and unchanged inputs verified.')
