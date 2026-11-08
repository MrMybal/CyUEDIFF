"""Verify whole cooked curve extraction and registered codec binding through an installed Core-only SDK."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def load(path, maximum=64 * 1024 * 1024):
    if path.stat().st_size > maximum:
        raise ValueError('Validation JSON exceeds read budget')
    return json.loads(path.read_text(encoding='utf-8'))


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def absolute(value):
    p = Path(value)
    if not p.is_absolute() or not p.is_file():
        raise ValueError('Use an explicit absolute existing input')
    return p.resolve()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--tolerance', type=float, default=3e-5)
    args = parser.parse_args()
    if not math.isfinite(args.tolerance) or args.tolerance < 0:
        parser.error('Tolerance must be finite and nonnegative')
    manifest_path = absolute(str(args.manifest.resolve()))
    manifest = load(manifest_path)
    if manifest['schema'] != 'cyuediff.cooked-curve-validation/1' or not 1 <= len(manifest['cases']) <= 100:
        raise ValueError('Select 1..100 explicit whole-package validation cases')
    protected = {manifest_path: digest(manifest_path)}
    for row in manifest.get('protectedSources', []):
        p = absolute(row['path'])
        if digest(p) != row['sha256']:
            raise ValueError('A protected source differs before validation')
        protected[p] = row['sha256']
    for row in manifest['cases']:
        for key in ('container', 'global', 'context', 'reference'):
            p = absolute(row[key])
            protected[p] = digest(p)
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for i, row in enumerate(manifest['cases']):
        ref = load(absolute(row['reference']))
        if ref['schema'] != 'cyuediff.unreal-compressed-curves-reference/1' or not ref['engine'].startswith('5.3.'):
            raise ValueError('Require actual compressed UE 5.3 curve API values')
        if ref['codecClass'].endswith('UniformlySampled') and ref['runtimeNamePermutation'] != list(range(len(ref['names']))):
            raise ValueError('Uniform batch reference requires measured encoded-order name permutation')
        times = args.output/f'{i}-times.json'
        times.write_text(json.dumps(ref['times']), encoding='utf-8')
        target = args.output/f'{i}-actual.json'
        cmd = [str(args.consumer.resolve()), row['container'], row['entry'], row['global'], row['context'], row['curveSettingsEntry'], str(times), str(target)]
        p = subprocess.run(cmd, capture_output=True, timeout=60)
        if p.returncode:
            raise ValueError(p.stderr.decode('utf-8', 'replace'))
        actual = load(target)
        info = actual['animation']['curves']
        if info['status'] != 'decoded' or info['codec']['class'] != ref['codecClass'] or info['settings']['package'] != ref['codecPath'].split('.')[0]:
            raise ValueError('Registered whole-package curve codec identity differs')
        if info['codecPath'] != ref['codecPath'] or info['names'] != ref['names']:
            raise ValueError('Stored codec path or encoded curve names differ')
        raw = bytes(ref['buffer'])
        if info['sha256'] != hashlib.sha256(raw).hexdigest() or info['decoded']['bufferIoHash'] != ref['bufferIoHash'] or info['bytes'] != len(raw):
            raise ValueError('Complete cooked curve buffer differs from independently exported codec input')
        if len(actual['samples']) != len(ref['times']):
            raise ValueError('Curve sample cardinality differs')
        maximum = 0.
        count = 0
        for t, sample, values in zip(ref['times'], actual['samples'], ref['samples']):
            if not sample['ready'] or not sample['storedCurvesKnown'] or sample['time'] != t or set(sample['values']) != {'name:'+n for n in ref['names']}:
                raise ValueError('Whole scalar sample time or identity coverage differs')
            for index, expected in enumerate(values):
                value = sample['values']['name:'+expected['name']]
                if value['index'] != index or not value['ready'] or not expected['batchPresent']:
                    raise ValueError('Scalar curve binding differs')
                a, b = value['value'], expected['batch']
                if not math.isfinite(a) or not math.isfinite(b):
                    raise ValueError('Non-finite scalar comparison')
                delta = abs(a-b)
                maximum = max(maximum, delta)
                count += 1
                if delta > args.tolerance:
                    raise ValueError(f'Whole cooked scalar mismatch at {t}: {expected["name"]}')
        results.append({'object': actual['animation']['source']['object'], 'codec': info['codec']['class'],
                        'settingsPackageSha256': info['settings']['packageSha256'], 'curves': len(ref['names']),
                        'values': count, 'maxDelta': maximum, 'curveBufferSha256': info['sha256']})
    if any(digest(p) != h for p, h in protected.items()):
        raise ValueError('A protected validation input changed')
    summary = {'schema': 'cyuediff.cooked-curve-validation-result/1', 'cases': results,
               'values': sum(r['values'] for r in results), 'maxDelta': max(r['maxDelta'] for r in results),
               'tolerance': args.tolerance, 'protectedInputs': len(protected)}
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
