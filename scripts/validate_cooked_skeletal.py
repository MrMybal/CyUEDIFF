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
import struct


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


def compare_morphs(actual, reference):
    """Validate stored compression against optional Engine source-API quantization.

    This checks emitted bytes and every stored index/quantized/decompressed value.
    It does not evaluate blend weights or runtime normal recomputation.
    """
    if reference.get('schema') != 'cyuediff.unreal-morph-reference/1' or not reference.get('engine', '').startswith('5.3.'):
        raise ValueError('Matching UE 5.3 native morph reference required')
    if reference.get('scope') != 'native-compression-input-quantization':
        raise ValueError('Unexpected morph reference scope')
    if actual.get('schema') != 'cyuediff.cooked-skeletal-mesh/1' or actual.get('profile') != 'ue5.3-windows':
        raise ValueError('Unexpected cooked skeletal schema/profile')
    if actual['object'] != reference['object'] or len(actual['lods']) != len(reference['lods']):
        raise ValueError('Morph object or LOD identities disagree')
    values = entries = targets = batches = 0
    for li, (lod, ref) in enumerate(zip(actual['lods'], reference['lods'])):
        model = lod['morphs']
        if not model['ready']:
            raise ValueError('Named morph bindings unavailable')
        native = model['buffer']
        raw = bytes(ref['nativeBytes'])
        if native['nativeBytes'] != len(raw) or native['nativeSha256'] != hashlib.sha256(raw).hexdigest():
            raise ValueError('Cooked morph native bytes differ from actual Engine serialization')
        if native['baseVertices'] != ref['vertices'] or len(model['targets']) != len(ref['targets']):
            raise ValueError('Morph target/vertex counts disagree')
        for key in ('positionPrecision', 'normalPrecision'):
            if native[key] != ref[key]:
                raise ValueError('Native morph precision differs')
        for source, stored in zip(ref['targets'], model['targets']):
            if not source.get('usesBuiltinCompression') or stored['name'] != source['name']:
                raise ValueError('Unknown compression or morph name mismatch')
            if any(not isinstance(d.get('normalRetained'), bool) for d in source['sourceDeltas']):
                raise ValueError('Reference normal retention is unspecified')
            expected = [d for d in source['sourceDeltas'] if any(d['quantizedPosition'] + d['quantizedNormal'])]
            expected.sort(key=lambda d: d['vertex'])
            if len(expected) != len(stored['deltas']):
                raise ValueError('Stored morph delta count differs from nonzero quantized source')
            targets += 1
            for a, b in zip(expected, stored['deltas']):
                for key in ('vertex', 'quantizedPosition', 'quantizedNormal'):
                    if a[key] != b[key]:
                        raise ValueError(f'LOD {li}, {source["name"]}: {key} differs')
                values += 7
                for key, quantized, precision in [('position', 'quantizedPosition', 'positionPrecision'), ('normal', 'quantizedNormal', 'normalPrecision')]:
                    for value, q in zip(b[key], a[quantized]):
                        if not math.isfinite(value) or not math.isclose(value, q * ref[precision], rel_tol=1e-7, abs_tol=1e-9):
                            raise ValueError('Stored float32 morph delta differs')
                        values += 1
                entries += 1
        batches += native['batches']
    return {'values': values, 'storedMorphDeltas': entries, 'morphTargets': targets, 'morphBatches': batches}


def compare_cloth(actual, reference):
    """Check stored cloth maps against Engine API and its exported record serializer.

    Physical simulation vertices, collision response and runtime cloth poses are
    outside this comparison. Complete native records, including padding, are
    checked separately from the semantic mapping projection.
    """
    if reference.get('schema') != 'cyuediff.unreal-cloth-reference/1' or not reference.get('engine', '').startswith('5.3.'):
        raise ValueError('Matching UE 5.3 cloth reference required')
    if actual.get('schema') != 'cyuediff.cooked-skeletal-mesh/1' or actual.get('profile') != 'ue5.3-windows':
        raise ValueError('Unexpected cooked skeletal schema/profile')
    if actual['object'] != reference['object'] or len(actual['lods']) != len(reference['lods']):
        raise ValueError('Cloth object or LOD identities disagree')
    def encode(records):
        data = bytearray()
        def scalar(value):
            if isinstance(value, dict) and set(value) == {'nonFiniteBits'}:
                return struct.pack('<I', value['nonFiniteBits'])
            return struct.pack('<f', value)
        try:
            for record in records:
                for key in ('position', 'normal', 'tangent'):
                    if len(record[key]) != 4:
                        raise ValueError('Cloth coefficient vector size differs')
                    for value in record[key]:
                        data.extend(scalar(value))
                if len(record['sourceIndices']) != 4:
                    raise ValueError('Cloth source index vector size differs')
                data.extend(struct.pack('<4H', *record['sourceIndices']))
                data.extend(scalar(record['weight']))
                data.extend(struct.pack('<I', record['padding']))
        except (TypeError, struct.error, OverflowError, KeyError) as error:
            raise ValueError('Invalid typed cloth record') from error
        return bytes(data)
    def api_bits(data):
        # UE's ordinary numeric JSON can print -0.0 as integer 0. The separate
        # native-record bytes and decoded Core buffer still require exact bits.
        result = bytearray(data)
        for at in range(0, len(result), 64):
            for offset in (*range(0, 48, 4), 56):
                if struct.unpack_from('<I', result, at + offset)[0] == 0x80000000:
                    struct.pack_into('<I', result, at + offset, 0)
        return bytes(result)
    c = Check()
    records = sections = mapped_vertices = 0
    for li, (lod, ref) in enumerate(zip(actual['lods'], reference['lods'])):
        model = lod.get('cloth')
        has_data = any(s['assetIndex'] != -1 or s['assetLOD'] != -1 or any(s['assetGuidWords']) or any(s['mappings']) for s in ref['sections'])
        if model is None:
            if has_data or ref['buffer'] or ref['indexMapping']:
                raise ValueError('Cloth data is absent from a nonempty native reference')
            continue
        if model.get('schema') != 'cyuediff.cloth/1' or model.get('scope') != 'stored-cloth-deformer-mapping':
            raise ValueError('Unexpected cloth projection scope')
        if model['simulationMeshBoundsVerified']:
            raise ValueError('Stored mapping validation cannot certify simulation mesh bounds')
        where = f'LOD{li}/cloth'
        c.equal(len(model['sections']), len(ref['sections']), where + '/sections')
        for section, source in zip(model['sections'], ref['sections']):
            for key in ('firstVertex', 'vertices', 'assetIndex', 'assetLOD', 'assetGuidWords'):
                if section[key] != source[key]:
                    raise ValueError('Cloth binding integer identity differs')
            if len(section['mappings']) != len(source['mappings']):
                raise ValueError('Cloth LOD bias inventory differs')
            for mapping, expected_mapping in zip(section['mappings'], source['mappings']):
                if api_bits(encode(mapping)) != api_bits(encode(expected_mapping)):
                    raise ValueError('Decoded section mapping bits differ from actual Engine API values')
            for key in ('firstVertex', 'vertices', 'assetIndex', 'assetLOD', 'assetGuidWords', 'mappings'):
                c.equal(section[key], source[key], where + '/section/' + str(section['section']) + '/' + key)
            sections += 1
            if source['mappings'] and source['mappings'][0]:
                mapped_vertices += source['vertices']
        if model['indexMapping'] != ref['indexMapping']:
            raise ValueError('Cloth buffer index identities differ')
        c.equal(model['indexMapping'], ref['indexMapping'], where + '/indexMapping')
        c.equal(model['buffer'], ref['buffer'], where + '/buffer')
        count = len(ref['buffer'])
        if count:
            if not model['bufferStored'] or ref['stride'] != 64:
                raise ValueError('Native cloth buffer is unavailable or has another stride')
            raw = bytes(ref['nativeRecordBytes'])
            if len(raw) != count * 64 or model['bufferInfo']['recordSha256'] != hashlib.sha256(raw).hexdigest():
                raise ValueError('Cloth record bytes differ from actual Engine serialization')
            if encode(model['buffer']) != raw or api_bits(encode(ref['buffer'])) != api_bits(raw):
                raise ValueError('Decoded cloth record bits differ from actual Engine serialization')
            records += count
        elif model['bufferStored']:
            raise ValueError('Unexpected stored cloth buffer')
    return {'values': c.values, 'clothSections': sections, 'clothRecords': records,
            'clothMappedVertices': mapped_vertices}


def compare_overlaps(actual, reference):
    """Exact native section overlap arrays, serializer bytes and resolved relations."""
    if actual.get('schema') != 'cyuediff.cooked-skeletal-mesh/1' or actual.get('profile') != 'ue5.3-windows':
        raise ValueError('Unexpected cooked skeletal schema/profile')
    if reference.get('schema') != 'cyuediff.unreal-overlaps-reference/1' or not reference.get('engine', '').startswith('5.3.'):
        raise ValueError('Matching UE 5.3 overlaps reference required')
    if actual.get('object') != reference.get('object') or len(actual['lods']) != len(reference['lods']):
        raise ValueError('Overlap object/LOD identity differs')
    values = sections = vertices = indices = links = 0

    def exact(a, b):
        nonlocal values
        if isinstance(b, list):
            if not isinstance(a, list) or len(a) != len(b):
                raise ValueError('Overlap array size differs')
            for x, y in zip(a, b):
                exact(x, y)
        elif type(a) is not int or type(b) is not int or a != b:
            raise ValueError('Overlap integer identity differs')
        else:
            values += 1

    for lod, ref in zip(actual['lods'], reference['lods']):
        if len(lod['sections']) != len(ref):
            raise ValueError('Overlap section inventory differs')
        model = lod['geometry'].get('vertexOverlaps', {})
        if model.get('schema') != 'cyuediff.mesh-overlaps/1' or model.get('scope') != 'stored-section-vertex-overlaps':
            raise ValueError('Unexpected overlap projection scope')
        if set(model['sections']) != {str(i) for i in range(len(ref))}:
            raise ValueError('Overlap semantic section inventory differs')
        for si, (section, expected) in enumerate(zip(lod['sections'], ref)):
            raw = section['vertexOverlaps']
            if raw.get('scope') != 'stored-section-vertex-overlaps' or any(raw.get(k) is not False for k in ('coincidenceVerified', 'symmetryVerified', 'runtimeTangentsEvaluated')):
                raise ValueError('Unexpected overlap semantic claims')
            for field in ('firstVertex', 'vertices', 'indices', 'windows'):
                exact(raw[field], expected[field])
            exact(raw['indicesStored'], len(expected['indices']))
            exact(raw['windowsStored'], len(expected['windows']))
            native = struct.pack('<i', len(raw['indices']))
            native += b''.join(struct.pack('<I', value) for value in raw['indices'])
            native += struct.pack('<i', len(raw['windows']))
            native += b''.join(struct.pack('<II', *window) for window in raw['windows'])
            exact(list(native), expected['nativeBytes'])
            if hashlib.sha256(native).hexdigest() != raw['serializedSha256']:
                raise ValueError('Overlap native serialization hash differs')
            start, count = raw['firstVertex'], raw['vertices']
            rows = {}
            if len(raw['windows']) != max(1, count) or not raw['indices']:
                raise ValueError('Overlap native dimensions differ')
            for vi, (length, offset) in enumerate(raw['windows'][:count]):
                if offset > len(raw['indices']) or length > len(raw['indices']) - offset:
                    raise ValueError('Overlap native window exceeds data')
                local = raw['indices'][offset:offset + length]
                if any(value < 0 or value >= count for value in local):
                    raise ValueError('Overlap index exceeds section')
                rows[str(start + vi)] = sorted(start + value for value in local)
                links += length
            actual_rows = model['sections'][str(si)]
            exact(actual_rows['firstVertex'], start)
            if set(actual_rows['vertices']) != set(rows):
                raise ValueError('Overlap vertex inventory differs')
            for vertex, neighbors in rows.items():
                exact(actual_rows['vertices'][vertex], neighbors)
            sections += 1
            vertices += count
            indices += len(raw['indices'])
    return {'values': values, 'overlapSections': sections, 'overlapVertices': vertices,
            'overlapIndices': indices, 'overlapLinks': links}


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
    morph_references = [(manifest_path.parent / row['morphReference']).resolve() if row.get('morphReference') else None for row in cases]
    cloth_references = [(manifest_path.parent / row['clothReference']).resolve() if row.get('clothReference') else None for row in cases]
    overlap_references = [(manifest_path.parent / row['overlapsReference']).resolve() if row.get('overlapsReference') else None for row in cases]
    protected = [args.consumer.resolve(), args.container.resolve(), args.global_toc.resolve(),
                 args.schema.resolve(), manifest_path, *references, *[p for p in morph_references + cloth_references + overlap_references if p]]
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
            if morph_references[i]:
                morphs = compare_morphs(actual, json.loads(morph_references[i].read_text(encoding='utf-8')))
                evidence['values'] += morphs.pop('values')
                evidence.update(morphs)
            if cloth_references[i]:
                cloth = compare_cloth(actual, json.loads(cloth_references[i].read_text(encoding='utf-8')))
                evidence['values'] += cloth.pop('values')
                evidence.update(cloth)
            if overlap_references[i]:
                overlaps = compare_overlaps(actual, json.loads(overlap_references[i].read_text(encoding='utf-8')))
                evidence['values'] += overlaps.pop('values')
                evidence.update(overlaps)
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
