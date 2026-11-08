"""Compare a Core-only cooked skeletal consumer against independent Unreal API exports.

The manifest is private JSON: {"cases":[{"entry":"24 hex chunk ID",
"reference":"relative/reference.json", "colors":false}]}.
The reference adapter is optional tools/unreal_reference; no Engine is run here.
"""
import argparse
import hashlib
import json
import math
import os
import re
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


class Check:
    def __init__(self):
        self.values = 0
        self.max_delta = 0.0

    def equal(self, actual, expected, where):
        if isinstance(expected, dict):
            if not isinstance(actual, dict):
                raise ValueError(where + ': expected object')
            for key, value in expected.items():
                self.equal(actual[key], value, where + '/' + key)
        elif isinstance(expected, list):
            if not isinstance(actual, list) or len(actual) != len(expected):
                raise ValueError(where + ': array sizes differ')
            for i, (a, b) in enumerate(zip(actual, expected)):
                self.equal(a, b, where + '/' + str(i))
        else:
            self.values += 1
            if isinstance(expected, (int, float)) and not isinstance(expected, bool):
                if not isinstance(actual, (int, float)) or not math.isfinite(actual):
                    raise ValueError(where + ': expected finite number')
                delta = abs(actual - expected)
                self.max_delta = max(self.max_delta, delta)
                if not math.isclose(actual, expected, abs_tol=2e-6, rel_tol=1e-7):
                    raise ValueError(f'{where}: {actual} != {expected}')
            elif actual != expected:
                raise ValueError(f'{where}: {actual!r} != {expected!r}')


def compare(actual, reference):
    if actual.get('schema') != 'cyuediff.cooked-skeletal-mesh/1' or actual.get('profile') != 'ue5.3-windows':
        raise ValueError('Unexpected cooked skeletal schema/profile')
    if reference.get('schema') != 'cyuediff.unreal-reference/1' or not reference.get('engine', '').startswith('5.3.'):
        raise ValueError('A matching UE 5.3 reference adapter export is required')
    c = Check()
    c.equal(actual['object'], reference['object'], 'object')
    if reference['class'] != '/Script/Engine.SkeletalMesh':
        raise ValueError('Reference is not a SkeletalMesh')
    c.equal(actual['bones'], reference['bones'], 'bones')
    c.equal(len(actual['lods']), len(reference['lods']), 'LODs')
    vertices = triangles = influences = 0
    for li, (lod, ref) in enumerate(zip(actual['lods'], reference['lods'])):
        tag = f'LOD{li}'
        c.equal(lod['sections'], ref['sections'], tag + '/sections')
        for field in ('requiredBones', 'activeBones'):
            c.equal(lod[field], ref[field], tag + '/' + field)
        for field in ('indexBytes', 'boneIndexBytes', 'boneWeightBytes', 'variableInfluences', 'maxInfluences'):
            c.equal(lod['buffers'][field], ref[field], tag + '/' + field)
        g = lod['geometry']
        if 'weightProfiles' in ref:
            expected_profiles = {p['name']: p for p in ref['weightProfiles']}
            actual_profiles = {p['name']: p for p in lod['weightProfiles']}
            c.equal(sorted(actual_profiles), sorted(expected_profiles), tag + '/weightProfiles/names')
            for name, profile in expected_profiles.items():
                if not profile.get('available') or 'resolvedInfluences' not in profile:
                    raise ValueError(tag + '/weightProfiles/' + name + ': actual Engine API values unavailable')
                stored = actual_profiles[name]
                where = tag + '/weightProfiles/' + name
                c.equal(stored['skinProfileKey'], 'profile:' + name, where + '/identity')
                c.equal(stored['influencesPerVertex'], profile['influencesPerVertex'], where + '/slots')
                ids, weights = bytes(profile['boneIDs']), bytes(profile['boneWeights'])
                c.equal(stored['buffers']['boneIDsSha256'], hashlib.sha256(ids).hexdigest(), where + '/IDs')
                c.equal(stored['buffers']['boneWeightsSha256'], hashlib.sha256(weights).hexdigest(), where + '/weights')
                entries = {str(vertex): record for vertex, record in profile['vertexToRecord']}
                c.equal(stored['vertexToRecord'], entries, where + '/vertexToRecord')
                c.equal(sorted(stored['overrides']), sorted(entries), where + '/overrideVertices')
                count = profile['influencesPerVertex']
                ib, wb = lod['buffers']['boneIndexBytes'], lod['buffers']['boneWeightBytes']
                for vertex, record in entries.items():
                    vi = int(vertex)
                    section = next(v for v in ref['sections'] if v['firstVertex'] <= vi < v['firstVertex'] + v['vertices'])
                    decoded = []
                    for slot in range(count):
                        offset = (record * count + slot)
                        bone = int.from_bytes(ids[offset * ib:(offset + 1) * ib], 'little')
                        weight = int.from_bytes(weights[offset * wb:(offset + 1) * wb], 'little')
                        decoded.append({'bone': section['boneMap'][bone], 'rawWeight': weight * (257 if wb == 1 else 1)})
                    c.equal(stored['overrides'][vertex], decoded, where + '/stored/' + vertex)
                c.equal(len(profile['resolvedInfluences']), len(ref['vertices']), where + '/resolvedVertices')
                for vi, row in enumerate(profile['resolvedInfluences']):
                    section = next(v for v in ref['sections'] if v['firstVertex'] <= vi < v['firstVertex'] + v['vertices'])
                    expected = {actual['bones'][section['boneMap'][bone]]['name']: weight for bone, weight in row if weight}
                    c.equal(g['skin']['vertices'][str(vi)][stored['skinProfileKey']], expected, where + '/actualRuntime/' + str(vi))
        c.equal(len(g['vertices']), len(ref['vertices']), tag + '/vertices')
        vertices += len(ref['vertices'])
        triangles += len(g['triangles'])
        expected_indices = []
        for ti in range(len(g['triangles'])):
            expected_indices.extend(g['triangles'][str(ti)]['instances'])
        c.equal(expected_indices, ref['indices'], tag + '/indices')
        bones = actual['bones']
        for i, v in enumerate(ref['vertices']):
            where = tag + '/vertex/' + str(i)
            inst = g['instances'][str(i)]
            c.equal(g['vertices'][str(i)]['position'], v['position'], where + '/position')
            c.equal(inst['tangent'], v['tangentX'][:3], where + '/tangent')
            c.equal(inst['normal'], v['tangentZ'][:3], where + '/normal')
            c.equal(inst['binormalSign'], v['tangentZ'][3], where + '/binormalSign')
            c.equal(inst['uvs'], v['uvs'], where + '/uvs')
            if 'color' in v:
                c.equal(inst['color'], [x / 255 for x in v['color']], where + '/color')
            section = next(s for s in ref['sections'] if s['firstVertex'] <= i < s['firstVertex'] + s['vertices'])
            weights = {bones[section['boneMap'][b]]['name']: w for b, w in v['influences'] if w}
            semantic = g['skin']['vertices'][str(i)]['default']
            c.equal(sorted(semantic), sorted(weights), where + '/skin/boneNames')
            c.equal(semantic, weights, where + '/skin')
            influences += len(weights)
    return {'values': c.values, 'maxAbsoluteDelta': c.max_delta, 'vertices': vertices,
            'triangles': triangles, 'nonzeroInfluences': influences}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for field in ('manifest', 'consumer', 'container', 'global-toc', 'schema', 'output-dir'):
        parser.add_argument('--' + field, required=True, type=Path)
    parser.add_argument('--codec', type=Path)
    parser.add_argument('--keep-mobile-min-lod', choices=('0', '1'), required=True)
    args = parser.parse_args()
    manifest_path = args.manifest.resolve()
    cases = json.loads(manifest_path.read_text(encoding='utf-8'))['cases']
    if not cases or len(cases) > 10000 or any(not re.fullmatch(r'[0-9a-fA-F]{24}', row['entry']) for row in cases):
        parser.error('Select 1..10000 explicit 24-hex package chunk IDs')
    references = [(manifest_path.parent / row['reference']).resolve() for row in cases]
    protected = [args.consumer.resolve(), args.container.resolve(), args.global_toc.resolve(),
                 args.schema.resolve(), manifest_path, *references]
    for toc in (args.container, args.global_toc):
        protected.extend(p.resolve() for p in toc.parent.glob(toc.stem + '*.ucas'))
    schema = json.loads(args.schema.read_text(encoding='utf-8'))
    if schema.get('usmap'):
        protected.append((args.schema.parent / schema['usmap']).resolve())
    if args.codec:
        protected.append(args.codec.resolve())
    output = args.output_dir.resolve()
    if any(output == p or output in p.parents for p in protected):
        parser.error('Output directory contains a protected input')
    output.mkdir(parents=True, exist_ok=False)
    hashes = {str(p): digest(p) for p in set(protected)}
    env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
    reports = []
    try:
        for i, (row, ref_path) in enumerate(zip(cases, references)):
            colors = row.get('colors', 'stored')
            color_arg = ('1' if colors else '0') if isinstance(colors, bool) else colors
            if color_arg not in ('0', '1', 'stored'):
                raise ValueError('Each colors setting must be a bool or stored')
            command = [str(args.consumer.resolve()), str(args.container.resolve()), row['entry'],
                       str(args.global_toc.resolve()), str(args.schema.resolve()),
                       args.keep_mobile_min_lod, color_arg]
            if args.codec:
                command.append(str(args.codec.resolve()))
            result = subprocess.run(command, env=env, capture_output=True, timeout=120)
            if result.returncode:
                raise RuntimeError(result.stderr.decode('utf-8', 'replace'))
            actual = json.loads(result.stdout)
            reference = json.loads(ref_path.read_text(encoding='utf-8'))
            evidence = compare(actual, reference)
            evidence.update(entry=row['entry'], object=actual['object'], referenceEngine=reference['engine'])
            reports.append(evidence)
            (output / f'{i:03d}.json').write_text(json.dumps(actual, indent=2), encoding='utf-8')
    finally:
        changed = [path for path, original in hashes.items() if digest(Path(path)) != original]
        if changed:
            raise RuntimeError('Protected inputs changed: ' + ', '.join(changed))
    summary = {'schema': 'cyuediff.cooked-skeletal-validation/1', 'cases': reports,
               'inputSha256': hashes, 'inputsUnchanged': True,
               'values': sum(r['values'] for r in reports),
               'vertices': sum(r['vertices'] for r in reports),
               'triangles': sum(r['triangles'] for r in reports)}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    print(json.dumps({k: summary[k] for k in ('values', 'vertices', 'triangles', 'inputsUnchanged')}))


if __name__ == '__main__':
    main()
