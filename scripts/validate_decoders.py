"""Validate the separately installed decoder API on controlled fixtures and optional local assets."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--consumer', required=True, type=Path)
parser.add_argument('--cli', required=True, type=Path)
parser.add_argument('--fixtures', required=True, type=Path)
parser.add_argument('--sample', action='append', default=[], type=Path)
parser.add_argument('--out', type=Path, default=Path('build-native/decoder-validation.json'))
args = parser.parse_args()

def run(executable, arguments, code=0):
    result = subprocess.run([str(executable.resolve()), *map(str, arguments)], capture_output=True,
                            text=True, encoding='utf-8')
    assert result.returncode == code, (result.returncode, result.stderr)
    return json.loads(result.stdout)

def files(asset):
    return [asset] + [p for extension in ('.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload')
                     if (p := asset.with_suffix(extension)).is_file()]

def fingerprint(paths):
    return {str(path.resolve()): hashlib.sha256(path.read_bytes()).hexdigest() for path in paths}

before, after = (args.fixtures / f'{name}.uasset' for name in ('before', 'after'))
inputs = [p for asset in [before, after, *args.sample] for p in files(asset)]
assert args.out.resolve() not in {p.resolve() for p in [*inputs, args.consumer, args.cli]}, 'Output overlaps input'
originals = fingerprint(inputs)
changed = run(args.consumer, [before, after], 1)
assert changed['status'] == 'changed' and changed['completeInput']
assert changed['changes'] == [{'op': 'replace', 'path': '/objects/TestAsset/properties/Items/value/data/number',
                              'before': 42, 'after': 44}]
assert run(args.consumer, [before, before])['status'] == 'identical'
regressions = []
for asset in args.sample:
    plain = run(args.cli, ['inspect', asset])
    empty = run(args.consumer, ['--inspect-empty-registry', asset])
    assert plain == empty, f'Empty registry changed output: {asset}'
    assert plain['completeInput']
    regressions.append({'file': str(asset), 'exports': plain['summary']['exportCount'],
                        'emptyRegistryPreservesOutput': True})
assert originals == fingerprint(inputs), 'Reader modified inputs'
report = {'schema': 'cyuediff.decoder-validation/1', 'controlledDiff': changed,
          'selfDiff': 'identical', 'installedCoreConsumer': True,
          'realAssets': regressions, 'inputsUnchanged': True}
args.out.parent.mkdir(parents=True, exist_ok=True)
args.out.write_text(json.dumps(report, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
print(f'Installed decoder API verified; one controlled field diff, self diff, {len(regressions)} real asset regressions; inputs unchanged.')
