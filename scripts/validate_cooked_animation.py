"""Validate whole cooked AnimSequence extraction and verified Skeleton names through an installed Core SDK.

The optional Unreal adapter independently exports direct compressed-codec poses and Skeleton API values.
All host-owned inputs and generated evidence belong in a private ignored build directory.
"""
import argparse
import json
import math
from pathlib import Path
import subprocess
from validate_per_track_animation import absolute, file_hash, load


def verify_case(consumer, row, destination, tolerance):
    container = absolute(row['container'])
    global_table = absolute(row['global'])
    schema = absolute(row['context'])
    reference_path = absolute(row['reference'])
    skeleton_path = absolute(row['skeletonReference'])
    expected = load(reference_path)
    skeleton = load(skeleton_path)
    if expected['schema'] != 'cyuediff.unreal-compressed-animation-reference/1' or not expected['engine'].startswith('5.3.'):
        raise ValueError('Require a direct compressed-codec UE 5.3 reference')
    if expected['boneCodecClass'] != '/Script/Engine.AnimCompress_PerTrackCompression' or expected['interpolation'] != 0:
        raise ValueError('This example explicitly selects linear per-track compression')
    if skeleton['schema'] != 'cyuediff.unreal-reference/1' or skeleton['class'] != '/Script/Engine.Skeleton':
        raise ValueError('Require an independent actual Skeleton API reference')
    keys = expected['keys']
    if type(keys) is not int or not 1 <= keys <= 10000 or expected['sampleFrames'] != [i*.5 for i in range(2*keys-1)]:
        raise ValueError('Reference must cover every integer and half frame')
    if not expected['tracks'] or any(len(t[field]) != 2*keys-1 for t in expected['tracks'] for field in ('translations', 'rotations', 'scales')):
        raise ValueError('Reference channel cardinalities differ')
    protected = {p: file_hash(p) for p in (container, global_table, schema, reference_path, skeleton_path)}
    subprocess.run([str(consumer), str(container), row['entry'], str(global_table), str(schema),
                    row['boneSettingsEntry'], row['skeletonEntry'], str(destination)], check=True,
                   capture_output=True, text=True, encoding='utf-8', timeout=120)
    actual = load(destination, 256*1024*1024)
    info = actual['animation']
    if info['schema'] != 'cyuediff.cooked-animation/1' or info['source']['object'] != expected['object']:
        raise ValueError('Cooked schema or actual animation object differs')
    if not info['boneChannelsDecoded'] or not info['identitiesComplete'] or info['unavailable'] is not None:
        raise ValueError('Complete compressed channel extraction and verified identities are required')
    if info['boneCodec']['status'] != 'verified' or info['boneCodec']['codec']['class'] != expected['boneCodecClass'] or info['boneCodec']['canonicalHandle'] != expected['boneCodecHandle']:
        raise ValueError('Actual verified registered codec identity differs')
    if info['animation']['bufferIoHash'] != expected['compressedBufferIoHash'] or info['animation']['bufferBytes'] != expected['compressedBytes']:
        raise ValueError('Extracted complete compressed buffer differs from the independent codec buffer')
    if info['trackToSkeleton'] != [track['skeletonIndex'] for track in expected['tracks']]:
        raise ValueError('Native compressed track map differs')
    binding = info['skeleton']
    if binding['status'] != 'verified' or binding['object'] != skeleton['object'] or len(binding['bones']) != len(skeleton['bones']):
        raise ValueError('Verified Skeleton object/bone count differs')
    wire_guid = bytes.fromhex(binding['guid'])
    guid = ''.join(f'{int.from_bytes(wire_guid[i:i+4], "little"):08x}' for i in range(0, 16, 4))
    if len(wire_guid) != 16 or guid != skeleton['skeletonGuid'].lower() or info['prefix']['skeletonGuid'] != binding['guid']:
        raise ValueError('Skeleton GUID does not agree with the animation and independent API reference')
    skeleton_components = 0
    maximum_skeleton = 0.
    for index, (bone, target) in enumerate(zip(binding['bones'], skeleton['bones'])):
        if bone['id'] != index or bone['name'] != target['name'] or bone['parent'] != target['parent']:
            raise ValueError('Native Skeleton identity or hierarchy differs')
        for field, width in (('translation', 3), ('rotation', 4), ('scale', 3)):
            if len(bone[field]) != width or len(target[field]) != width:
                raise ValueError('Reference Skeleton channels differ')
            for a, b in zip(bone[field], target[field]):
                if not math.isfinite(a) or not math.isfinite(b):
                    raise ValueError('Non-finite Skeleton reference')
                delta = abs(a-b)
                maximum_skeleton = max(maximum_skeleton, delta)
                skeleton_components += 1
                if delta > tolerance:
                    raise ValueError('Native Skeleton reference transform differs')
    if len(actual['samples']) != len(expected['sampleFrames']):
        raise ValueError('Cooked sample cardinality differs')
    count = 0
    maximum = 0.
    expected_names = {'name:'+track['bone'] for track in expected['tracks']}
    for index, sample in enumerate(actual['samples']):
        if not sample['ready'] or not sample['identitiesComplete'] or sample['frame'] != expected['sampleFrames'][index] or set(sample['tracks']) != expected_names:
            raise ValueError('Cooked sample frame or exact named identity set differs')
        if len(sample['numericTracks']) != len(expected['tracks']):
            raise ValueError('Numeric track cardinality differs')
        for track_index, track in enumerate(expected['tracks']):
            pose = sample['tracks']['name:'+track['bone']]
            numeric = sample['numericTracks'][track_index]
            if pose['skeletonIndex'] != track['skeletonIndex'] or not pose['ready'] or numeric['skeletonIndex'] != pose['skeletonIndex']:
                raise ValueError('Named/numeric compressed track binding differs')
            if numeric['translation'] != pose['positions'] or numeric['rotation'] != pose['rotations'] or numeric['scale'] != pose['scales']:
                raise ValueError('Numeric and named channel projections differ')
            for field, reference_field, width in (('positions', 'translations', 3), ('rotations', 'rotations', 4), ('scales', 'scales', 3)):
                values = pose[field]
                targets = track[reference_field][index]
                if len(values) != width or len(targets) != width or not all(math.isfinite(v) for v in values+targets):
                    raise ValueError('Malformed/non-finite compressed sample')
                if field == 'rotations' and sum(a*b for a, b in zip(values, targets)) < 0:
                    targets = [-v for v in targets]
                for a, b in zip(values, targets):
                    delta = abs(a-b)
                    maximum = max(maximum, delta)
                    count += 1
                    if delta > tolerance:
                        raise ValueError(f'Compressed pose mismatch: frame {sample["frame"]}, bone {track["bone"]}, {field}')
    for path, digest in protected.items():
        if file_hash(path) != digest:
            raise ValueError('A validation input changed')
    return {'object': info['source']['object'], 'frames': keys, 'tracks': len(expected['tracks']),
            'components': count, 'maxComponentDelta': maximum, 'skeletonComponents': skeleton_components,
            'maxSkeletonDelta': maximum_skeleton, 'packageSha256': info['source']['packageSha256'],
            'bufferIoHash': info['animation']['bufferIoHash'], 'skeletonPackageSha256': binding['packageSha256'],
            'settingsPackageSha256': info['boneCodec']['settings']['packageSha256']}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New private result directory')
    parser.add_argument('--tolerance', type=float, default=2e-6)
    args = parser.parse_args()
    if not math.isfinite(args.tolerance) or args.tolerance < 0:
        parser.error('Tolerance must be finite and nonnegative')
    consumer = absolute(str(args.consumer.resolve()))
    manifest = load(absolute(str(args.manifest.resolve())))
    if manifest['schema'] != 'cyuediff.cooked-animation-validation/1' or not 1 <= len(manifest['cases']) <= 100:
        raise ValueError('Use an explicit cooked animation validation manifest with 1..100 cases')
    protected = [(absolute(row['path']), row['sha256']) for row in manifest.get('protectedSources', [])]
    for path, digest in protected:
        if file_hash(path) != digest:
            raise ValueError('Protected source SHA256 differs before validation')
    args.output.mkdir(parents=True, exist_ok=False)
    results = [verify_case(consumer, row, args.output/f'case-{i}.json', args.tolerance) for i, row in enumerate(manifest['cases'])]
    for path, digest in protected:
        if file_hash(path) != digest:
            raise ValueError('Protected source SHA256 differs after validation')
    summary = {'schema': 'cyuediff.cooked-animation-validation-result/1', 'cases': results,
               'components': sum(row['components'] for row in results),
               'skeletonComponents': sum(row['skeletonComponents'] for row in results),
               'maxComponentDelta': max(row['maxComponentDelta'] for row in results),
               'maxSkeletonDelta': max(row['maxSkeletonDelta'] for row in results),
               'tolerance': args.tolerance, 'protectedSources': len(protected)}
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
