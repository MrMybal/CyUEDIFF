"""Validate explicit scalar curve buffers against independent Unreal codec API references."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess

CLASSES = {'/Script/Engine.AnimCurveCompressionCodec_CompressedRichCurve': 'rich',
           '/Script/Engine.AnimCurveCompressionCodec_UniformIndexable': 'indexable',
           '/Script/Engine.AnimCurveCompressionCodec_UniformlySampled': 'uniform'}


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def absolute(value):
    p = Path(value)
    if not p.is_absolute() or not p.is_file():
        raise ValueError('An existing absolute input file is required')
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
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    if manifest['schema'] != 'cyuediff.compressed-curve-validation/1' or not 1 <= len(manifest['cases']) <= 100:
        raise ValueError('Select 1..100 explicit validation cases')
    protected = {manifest_path: digest(manifest_path)}
    for row in manifest.get('protectedSources', []):
        p = absolute(row['path'])
        if digest(p) != row['sha256']:
            raise ValueError('Protected input differs before validation')
        protected[p] = row['sha256']
    for row in manifest['cases']:
        p = absolute(row['reference'])
        protected[p] = digest(p)
    args.output.mkdir(parents=True, exist_ok=False)
    cases = []
    for i, row in enumerate(manifest['cases']):
        reference_path = absolute(row['reference'])
        if reference_path.stat().st_size > 64 * 1024 * 1024:
            raise ValueError('Reference JSON exceeds 64 MiB')
        ref = json.loads(reference_path.read_text(encoding='utf-8'))
        if ref['schema'] != 'cyuediff.unreal-compressed-curves-reference/1' or not ref['engine'].startswith('5.3.'):
            raise ValueError('Require an actual UE 5.3 compressed-curve API reference')
        codec = CLASSES[ref['codecClass']]
        if codec == 'uniform' and ref['runtimeNamePermutation'] != list(range(len(ref['names']))):
            raise ValueError('UniformlySampled batch correspondence requires the measured encoded-order name permutation')
        raw = bytes(ref['buffer'])
        if len(raw) != ref['bufferBytes']:
            raise ValueError('Reference buffer length differs')
        meta = {'schema': 'cyuediff.compressed-curve-input/1', 'names': ref['names'], 'times': ref['times']}
        meta_path = args.output/f'{i}-input.json'
        raw_path = args.output/f'{i}-buffer.bin'
        target = args.output/f'{i}-actual.json'
        meta_path.write_text(json.dumps(meta), encoding='utf-8')
        raw_path.write_bytes(raw)
        result = subprocess.run([str(args.consumer.resolve()), codec, str(meta_path), str(raw_path), str(target)],
                                capture_output=True, timeout=60)
        if result.returncode:
            raise ValueError(result.stderr.decode('utf-8', 'replace'))
        actual = json.loads(target.read_text(encoding='utf-8'))
        info = actual['metadata']
        if info['bufferIoHash'] != ref['bufferIoHash'] or info['bufferBytes'] != len(raw) or info['bufferSha256'] != hashlib.sha256(raw).hexdigest():
            raise ValueError('Complete compressed curve buffer identity differs')
        if [c['name'] for c in info['curves']] != ref['names'] or len(actual['samples']) != len(ref['times']):
            raise ValueError('Curve names/sample count differ')
        maximum = 0.
        count = 0
        for time, expected, sample in zip(ref['times'], ref['samples'], actual['samples']):
            if sample['time'] != time or len(sample['values']) != len(expected):
                raise ValueError('Selected time or value cardinality differs')
            for value, target_value in zip(sample['values'], expected):
                a, b = value['value'], target_value['batch']
                if value['name'] != target_value['name'] or not target_value['batchPresent'] or not math.isfinite(a) or not math.isfinite(b):
                    raise ValueError('Invalid actual named curve value')
                delta = abs(a-b)
                maximum = max(maximum, delta)
                count += 1
                if delta > args.tolerance:
                    raise ValueError(f'Curve value mismatch at {time}, {value["name"]}: {delta}')
        cases.append({'object': ref['object'], 'codec': codec, 'curves': len(ref['names']),
                      'samples': len(ref['times']), 'values': count, 'maxDelta': maximum,
                      'bufferIoHash': info['bufferIoHash'], 'bufferSha256': info['bufferSha256']})
    if any(digest(p) != h for p, h in protected.items()):
        raise ValueError('A protected input changed')
    summary = {'schema': 'cyuediff.compressed-curve-validation-result/1', 'cases': cases,
               'values': sum(c['values'] for c in cases), 'maxDelta': max(c['maxDelta'] for c in cases),
               'protectedInputs': len(protected), 'tolerance': args.tolerance}
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
