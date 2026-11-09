"""Validate typed tagged-source physical cloth with actual UE 5.3 API references.

The installed Core-only consumer supplies stored geometry and certified native tails.
Class/map defaults and live simulation remain explicitly unavailable.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
from validate_physical_cloth_geometry import compare as compare_geometry


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def need(ok, message):
    if not ok:
        raise ValueError(message)


def compare(actual, reference, mesh, source_sha, decoder_sha, type_sha, max_properties=100000):
    need(reference.get('schema') == 'cyuediff.unreal-physical-cloth-geometry-reference/1' and reference.get('engine', '').startswith('5.3.2-'), 'Select an actual UE 5.3.2 API reference')
    provenance = actual.get('provenance', {})
    need(provenance.get('storage') == 'tagged-source' and provenance.get('completeInput') is True, 'An intact tagged source is required')
    need(provenance.get('decoderContextSha256') == decoder_sha, 'Exact native/type catalogue provenance differs')
    context = provenance.get('sourceTypeContext', {})
    need(context == {'sha256': type_sha, 'profileMatched': True, 'defaultsResolved': False} and type(context.get('profileMatched')) is bool and type(context.get('defaultsResolved')) is bool, 'Matching source type metadata is required')
    for key, value in [('ue4Version', 522), ('ue5Version', 1009), ('licenseeVersion', 0)]:
        need(type(provenance.get(key)) is int and provenance[key] == value, 'Source archive profile differs')
    need(not any(key in provenance for key in ['scriptObjectsSha256', 'schemaContextSha256', 'cookedPackageSha256']), 'Source provenance must not imply cooked data')
    policy = provenance.get('sourceReadLimits')
    if max_properties == 100000:
        need(policy is None, 'Default source policy must be implicit')
    else:
        need(policy == {'maxProperties': max_properties, 'maxPropertyBytes': 33554432} and all(type(x) is int for x in policy.values()), 'Exact explicit source policy differs')
    need(provenance.get('fileFingerprints', {}).get('package', {}).get('sha256') == source_sha, 'Original source bytes differ')
    need(type(actual.get('configMapReplaced')) is bool and not actual['configMapReplaced'] and actual.get('defaultsResolved') is False and actual.get('simulationEvaluated') is False, 'Source defaults/simulation must remain unresolved')
    need(all(x.get('weightMapReplaced') is False for x in actual.get('lods', [])), 'Stored source map deltas do not imply replacement')
    need(isinstance(provenance.get('nativeDecoderIds'), list) and len(provenance['nativeDecoderIds']) == 2 and all(isinstance(x, str) and x for x in provenance['nativeDecoderIds']), 'Selected native serializer identities are required')
    need(actual.get('available') is True, 'Physical source projection is unavailable')
    return compare_geometry(actual, reference, mesh)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args()
    root = args.manifest.resolve().parent
    def path(value):
        return (root / value).resolve(strict=True)
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    cases = manifest['cases']
    need(isinstance(cases, list) and 1 <= len(cases) <= 1000, 'Select 1..1000 explicit cases')
    work = manifest.get('maxProperties', 100000)
    need(type(work) is int and 1 <= work <= 4000000, 'Invalid explicit source policy')
    catalogue = path(manifest['layouts'])
    protected = {args.manifest.resolve(), args.consumer.resolve(), catalogue, Path(__file__).resolve(), Path(__file__).with_name('validate_physical_cloth_geometry.py').resolve()}
    for row in cases:
        protected.update([path(row['asset']), path(row['reference'])])
        for extension in ['.uexp', '.ubulk', '.uptnl', '.upayload']:
            companion = path(row['asset']).with_suffix(extension)
            if companion.is_file():
                protected.add(companion)
    before = {str(p): digest(p) for p in protected}
    out = args.output_dir.resolve()
    need(not out.exists() and not any(p == out or p.is_relative_to(out) for p in protected), 'Output must be a new independent directory')
    out.mkdir(parents=True)
    env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
    result = []
    for i, row in enumerate(cases):
        p = subprocess.run([str(args.consumer.resolve()), '--source', str(path(row['asset'])), str(catalogue), '--max-properties', str(work)], env=env, capture_output=True, timeout=180)
        need(p.returncode == 0, 'Core source consumer failed: ' + p.stderr.decode('utf-8', 'replace')[:1500])
        actual = json.loads(p.stdout)['before']
        reference = json.loads(path(row['reference']).read_text(encoding='utf-8'))
        result.append(compare(actual, reference, row['actualMesh'], before[str(path(row['asset']))], manifest['decoderSha256'], manifest['typeSha256'], work))
        (out / (format(i, '03d') + '.json')).write_bytes(p.stdout)
        print(str(row['actualMesh']) + ': ' + str(result[-1]['values']) + ' stored values', flush=True)
    need(all(digest(p) == h for p, h in before.items()), 'Protected input changed during validation')
    summary = {'schema': 'cyuediff.source-physical-validation/1', 'cases': result, 'inputSha256': before, 'inputsUnchanged': True}
    for key in ['values', 'vertices', 'triangles', 'tethers', 'transitionRecords', 'nativeBytes', 'configValues', 'wrapComponents']:
        summary[key] = sum(x[key] for x in result)
    summary['maxWrapError'] = max(x['maxWrapError'] for x in result)
    with (out / 'summary.json').open('w', encoding='utf-8', newline='\n') as stream:
        json.dump(summary, stream, indent=2)
        stream.write('\n')


if __name__ == '__main__':
    main()
