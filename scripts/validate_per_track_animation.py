"""Compare an installed Core-only per-track decoder with independent compressed-codec API exports.

Inputs are private metadata/buffers prepared from an explicit verified codec and view layout.
This does not discover package boundaries, infer codecs, bind a Skeleton or decode curves.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import subprocess


def load(path, maximum=64 * 1024 * 1024):
    if path.stat().st_size > maximum:
        raise ValueError('Validation JSON exceeds read limit')
    return json.loads(path.read_text(encoding='utf-8'))


def absolute(value):
    path = Path(value)
    if not path.is_absolute() or not path.is_file():
        raise ValueError('Validation inputs must be explicit absolute existing files')
    return path


def file_hash(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def validate_case(consumer, row, destination, tolerance):
    metadata = absolute(row['input'])
    buffer = absolute(row['buffer'])
    reference = absolute(row['reference'])
    input_data = load(metadata, 16 * 1024 * 1024)
    expected = load(reference)
    if expected['schema'] != 'cyuediff.unreal-compressed-animation-reference/1':
        raise ValueError('Use a direct compressed-codec API reference, not source-model samples')
    if expected['boneCodecClass'] != '/Script/Engine.AnimCompress_PerTrackCompression':
        raise ValueError('Unsupported reference codec class')
    if not expected['engine'].startswith('5.3.'):
        raise ValueError('Reference producer disagrees with the explicit UE 5.3 profile')
    keys = expected['keys']
    if type(keys) is not int or not 1 <= keys <= 10000 or not 1 <= len(expected['tracks']) <= 100000:
        raise ValueError('Reference frame/track cardinalities exceed adapter contract')
    if expected['sampleFrames'] != [index * .5 for index in range(2 * keys - 1)]:
        raise ValueError('Reference must include every integer and half frame')
    if any(len(track[channel]) != len(expected['sampleFrames']) for track in expected['tracks']
           for channel in ('translations', 'rotations', 'scales')):
        raise ValueError('Reference channel cardinalities differ')
    if input_data['frames'] != expected['keys'] or input_data['sampleFrames'] != expected['sampleFrames']:
        raise ValueError('Metadata frame domain differs from independent reference')
    if bool(input_data.get('step', False)) != (expected['interpolation'] == 1) or expected['interpolation'] not in (0, 1):
        raise ValueError('Interpolation differs from independent reference')
    if input_data['trackToSkeleton'] != [track['skeletonIndex'] for track in expected['tracks']]:
        raise ValueError('Numeric track binding differs from independent reference')
    protected = {path: file_hash(path) for path in (metadata, buffer, reference)}
    subprocess.run([str(consumer), str(metadata), str(buffer), str(destination)], check=True,
                   capture_output=True, text=True, encoding='utf-8', timeout=120)
    actual = load(destination, 256 * 1024 * 1024)
    animation = actual['animation']
    if animation['bufferIoHash'] != expected['compressedBufferIoHash'] or animation['bufferBytes'] != expected['compressedBytes']:
        raise ValueError('Core buffer does not match the independent compressed-codec buffer')
    if animation['bufferSha256'] != protected[buffer]:
        raise ValueError('Core source hash differs from supplied buffer')
    if len(actual['samples']) != len(expected['sampleFrames']) or len(animation['tracks']) != len(expected['tracks']):
        raise ValueError('Core sample/track cardinalities differ')
    count = 0
    maximum = 0.
    for index, sample in enumerate(actual['samples']):
        if sample['frame'] != expected['sampleFrames'][index] or len(sample['tracks']) != len(expected['tracks']):
            raise ValueError('Core sample identity/cardinality differs')
        for track_index, pose in enumerate(sample['tracks']):
            track = expected['tracks'][track_index]
            if pose['track'] != track_index or pose['skeletonIndex'] != track['skeletonIndex']:
                raise ValueError('Core track identity differs')
            for channel, reference_channel, width in (('translation', 'translations', 3), ('rotation', 'rotations', 4), ('scale', 'scales', 3)):
                values = pose[channel]
                targets = track[reference_channel][index]
                if len(values) != width or len(targets) != width or not all(math.isfinite(v) for v in values + targets):
                    raise ValueError('Non-finite or malformed reference/Core components')
                if channel == 'rotation' and sum(a*b for a, b in zip(values, targets)) < 0:
                    targets = [-v for v in targets]
                for value, target in zip(values, targets):
                    delta = abs(value - target)
                    maximum = max(maximum, delta)
                    count += 1
                    if delta > tolerance:
                        raise ValueError(f'Codec mismatch at frame {sample["frame"]}, track {track_index}, {channel}: {delta}')
    for path, digest in protected.items():
        if file_hash(path) != digest:
            raise ValueError('A validation input changed')
    return {'components': count, 'tracks': len(expected['tracks']), 'samples': len(actual['samples']),
            'maxComponentDelta': maximum, 'bufferIoHash': animation['bufferIoHash'],
            'bufferSha256': animation['bufferSha256'], 'referenceSha256': protected[reference]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New private evidence directory')
    parser.add_argument('--tolerance', type=float, default=2e-6)
    args = parser.parse_args()
    if not math.isfinite(args.tolerance) or args.tolerance < 0:
        parser.error('Tolerance must be finite and nonnegative')
    consumer = absolute(str(args.consumer.resolve()))
    manifest = load(absolute(str(args.manifest.resolve())))
    if manifest['schema'] != 'cyuediff.per-track-animation-validation/1':
        raise ValueError('Unknown validation manifest')
    rows = manifest['cases']
    if not 1 <= len(rows) <= 100:
        raise ValueError('Select 1..100 explicit cases')
    protected = [(absolute(item['path']), item['sha256']) for item in manifest.get('protectedSources', [])]
    for path, digest in protected:
        if file_hash(path) != digest:
            raise ValueError('Protected source SHA256 differs before validation')
    args.output.mkdir(parents=True, exist_ok=False)
    results = [validate_case(consumer, row, args.output / f'case-{i}.json', args.tolerance)
               for i, row in enumerate(rows)]
    for path, digest in protected:
        if file_hash(path) != digest:
            raise ValueError('Protected source SHA256 differs after validation')
    summary = {'schema': 'cyuediff.per-track-animation-validation-result/1', 'cases': results,
               'components': sum(item['components'] for item in results),
               'maxComponentDelta': max(item['maxComponentDelta'] for item in results),
               'tolerance': args.tolerance, 'protectedSources': len(protected)}
    (args.output / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
