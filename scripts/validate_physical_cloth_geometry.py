"""Validate a Core-only physical model against actual Engine geometry/native/Chaos references."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess


def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()


def compare(actual, reference, actual_mesh, source_bounds=None):
    if reference.get('schema') != 'cyuediff.unreal-physical-cloth-geometry-reference/1' or not reference.get('engine', '').startswith('5.3.'):
        raise ValueError('Select an actual UE 5.3 geometry reference')
    if actual.get('schema') != 'cyuediff.physical-cloth/1' or actual.get('profile') != 'ue5.3-windows' or actual.get('available') is not True:
        raise ValueError('Unavailable or incompatible physical projection')
    if actual.get('defaultsResolved') is not False or actual.get('simulationEvaluated') is not False:
        raise ValueError('Unsupported default/simulation coverage claim')
    canonical = lambda s: s.replace(':', '.', 1)
    reference_mesh = canonical(reference['object'])
    if not actual_mesh.startswith('/') or not reference_mesh.startswith('/'):
        raise ValueError('Explicit source-copy object pairing is required')
    def paired(path):
        path = canonical(path)
        if not path.startswith(reference_mesh + '.'):
            raise ValueError('Reference child is outside the declared source copy')
        return actual_mesh + path[len(reference_mesh):]
    selected = [a for a in reference['assets'] if paired(a['object']) == actual['object']]
    if len(selected) != 1:
        raise ValueError('Physical object identity is missing or ambiguous')
    ref = selected[0]
    guid = ''.join(format(x, '08x') for x in ref['assetGuidWords'])
    if actual['assetGuid'] != guid or type(actual['configMapReplaced']) is not bool:
        raise ValueError('Physical GUID/map provenance differs')
    values = vertices = triangles = tethers = native_bytes = records = config_values = wrap_components = render_records = 0
    max_wrap_error = 0.0
    unavailable = []
    if not actual['configMapReplaced']: unavailable.append('configs/default-map-unresolved')
    def same(a, b, kind=None):
        nonlocal values
        if isinstance(b, list):
            if not isinstance(a, list) or len(a) != len(b): raise ValueError('Stored array inventory differs')
            for x, y in zip(a, b): same(x, y, kind)
        elif kind == 'float':
            if isinstance(a, bool) or isinstance(b, bool) or not isinstance(a, (int, float)) or not isinstance(b, (int, float)) or not math.isfinite(a) or not math.isfinite(b):
                raise ValueError('Expected finite physical float32')
            try:
                packed = struct.pack('<f', a)
            except (OverflowError, struct.error) as e: raise ValueError('Physical float32 exceeds range') from e
            if struct.unpack('<f', packed)[0] != a: raise ValueError('Core output is not exact float32')
            if (a != 0 or b != 0) and packed != struct.pack('<f', b): raise ValueError('Stored float32 differs')
            values += 1
        else:
            if type(a) is not type(b) or a != b: raise ValueError('Stored scalar/type differs')
            values += 1
    for name in ['lodMap', 'usedBoneIndices', 'usedBoneNames']:
        same(actual[name], ref[name])
    if actual['referenceBoneIndex'] is None: unavailable.append('referenceBoneIndex')
    else: same(actual['referenceBoneIndex'], ref['referenceBoneIndex'])
    if len(actual['lods']) != len(ref['lods']): raise ValueError('Physical LOD inventory differs')
    for i, (a, b) in enumerate(zip(actual['lods'], ref['lods'])):
        if not a['weightMapReplaced']: unavailable.append('lods/' + str(i) + '/default-weight-map-unresolved')
        for key in ['vertices', 'normals', 'indices', 'inverseMasses', 'selfCollisionIndices']:
            same(a[key], b[key], 'float' if key in ['vertices', 'normals', 'inverseMasses'] else None)
        vertices += len(a['vertices']); triangles += len(a['indices']) // 3
        if len(a['boneData']) != len(b['boneData']): raise ValueError('Bone inventory differs')
        for x, y in zip(a['boneData'], b['boneData']):
            same(x['influences'], y['influences']); same(x['indices'], y['indices']); same(x['weights'], y['weights'], 'float')
        expected_maps = {str(x['target']): x['values'] for x in b['weightMaps']}
        if set(a['weightMaps']) != set(expected_maps): raise ValueError('Selected real weight entries differ')
        for key, v in expected_maps.items(): same(a['weightMaps'][key], v, 'float')
        same(a['bounds']['min'], b['bounds']['min'], 'float'); same(a['bounds']['max'], b['bounds']['max'], 'float')
        if a.get('geometryAvailable') is not True: raise ValueError('Selected physical geometry is unavailable')
        for key, target, kind in [('MaxBoneWeights', 'maxBoneWeights', None), ('NumFixedVerts', 'fixedVertices', None), ('SkinningKernelRadius', 'skinningKernelRadius', 'float'), ('bUseMultipleInfluences', 'multipleInfluences', None), ('bSmoothTransition', 'smoothTransition', None)]:
            if key in a['parameters']: same(a['parameters'][key], b[target], kind)
            else: unavailable.append('lods/' + str(i) + '/' + key)
        for name in ['euclideanTethers', 'geodesicTethers']:
            raw_tethers = bytearray(struct.pack('<i', len(a[name])))
            if len(a[name]) != len(b[name]['batches']): raise ValueError('Tether batch inventory differs')
            for x, y in zip(a[name], b[name]['batches']):
                raw_tethers.extend(struct.pack('<i', len(x)))
                if len(x) != len(y): raise ValueError('Tether inventory differs')
                for u, v in zip(x, y):
                    same(u['anchor'], v[0]); same(u['particle'], v[1]); same(u['length'], v[2], 'float'); tethers += 1
                    raw_tethers.extend(struct.pack('<iif', u['anchor'], u['particle'], u['length']))
            if bytes(raw_tethers) != bytes(b[name]['nativeBytes']): raise ValueError('Exact native tether encoding differs')
            native_bytes += len(raw_tethers)
        for direction in ['Up', 'Down']:
            data = a['transition' + direction]['values']; expected = b['transition' + direction]
            if len(data) != len(expected): raise ValueError('Transition inventory differs')
            raw = bytearray(struct.pack('<i', len(data)))
            for x, y in zip(data, expected):
                for field in ['position', 'normal', 'tangent', 'weight']:
                    same(x[field], y[field], 'float')
                same(x['sourceIndices'], y['sourceIndices']); same(x['padding'], y['padding'])
                raw.extend(struct.pack('<12f4HfI', *(x['position'] + x['normal'] + x['tangent'] + x['sourceIndices'] + [x['weight'], x['padding']])))
                records += 1
            if bytes(raw) != bytes(b['transition' + direction + 'NativeBytes']): raise ValueError('Exact native transition encoding differs')
            native_bytes += len(raw)
            expected_wrap = b['wrap' + direction]; wrapped = a['wrap' + direction]
            if expected_wrap is None:
                if wrapped is not None: raise ValueError('An unbound transition acquired coordinates')
                continue
            if not a['transition' + direction]['sourceBoundsVerified'] or not a['transition' + direction]['wrapAvailable']:
                raise ValueError('Usable transition bounds differ')
            if len(wrapped) != len(expected_wrap['positions']): raise ValueError('Wrapped vertex inventory differs')
            source = expected_wrap['sourceLOD']; same(a['transition' + direction]['sourceLOD'], source)
            for vertex, (x, y) in enumerate(zip(wrapped, expected_wrap['positions'])):
                if len(x) != 3: raise ValueError('Wrapped vector width differs')
                mapping = expected[vertex]
                for k, (u, v) in enumerate(zip(x, y)):
                    if isinstance(u, bool) or not isinstance(u, (float, int)) or not math.isfinite(u): raise ValueError('Invalid wrapped coordinate')
                    scale = sum(abs(ref['lods'][source]['vertices'][mapping['sourceIndices'][j]][k] * mapping['position'][j]) + abs(ref['lods'][source]['normals'][mapping['sourceIndices'][j]][k] * mapping['position'][3]) for j in range(3))
                    # Bound rounding differences between scalar and vectorized/fused float32 expressions.
                    error = abs(u - v)
                    if error > 2e-6 * max(1.0, scale): raise ValueError('Actual Chaos transition coordinate differs')
                    max_wrap_error = max(max_wrap_error, error); wrap_components += 1
    actual_configs = {x['object']: x for x in actual['configs']}
    if set(actual_configs) != {paired(x['object']) for x in ref['configs']}: raise ValueError('Selected configuration identity differs')
    for cfg in ref['configs']:
        x = actual_configs[paired(cfg['object'])]
        if x['class'] != cfg['class'] or not x['available']: raise ValueError('Selected configuration class is unavailable')
        for field, value in cfg.items():
            if field in ('object', 'class'): continue
            if field not in x['values']:
                if field not in x['unavailableFields']: raise ValueError('Missing field has no unavailable diagnostic')
                unavailable.append(cfg['class'] + '.' + field); continue
            same(x['values'][field], value, None if field == 'IterationCount' else 'float'); config_values += 1
    if source_bounds is not None:
        expected = reference['renderMappings']
        matching = {(i, j) for i, lod in enumerate(expected['lods']) for j, section in enumerate(lod['sections'])
                    if section['assetGuidWords'] == ref['assetGuidWords'] and section['assetLOD'] >= 0 and section['mappings'] and section['mappings'][0]}
        actual_rows = {(x['renderLOD'], x['section']) for x in source_bounds}
        if actual_rows != matching or len(actual_rows) != len(source_bounds): raise ValueError('Render correspondence inventory differs')
        for row in source_bounds:
            if any(type(row[k]) is not int for k in ['renderLOD', 'section', 'physicalLOD', 'lodBias', 'physicalVertices', 'activeRecords', 'inactiveRecords']):
                raise ValueError('Invalid render correspondence integer')
            section = expected['lods'][row['renderLOD']]['sections'][row['section']]
            if row['sourceIdentityVerified'] is not True or row['sourceBoundsVerified'] is not True or row['lodBias'] != 0 or row['explicitBiasedSource'] is not False:
                raise ValueError('Base source identity/bounds proof differs')
            if section['assetGuidWords'] != ref['assetGuidWords'] or row['physicalObject'] != actual['object'] or row['physicalLOD'] != section['assetLOD']:
                raise ValueError('Stored render GUID/LOD binding differs')
            rows = section['mappings'][0]
            active = sum(x['sourceIndices'][3] != 65535 for x in rows)
            if row['activeRecords'] != active or row['inactiveRecords'] != len(rows) - active or row['physicalVertices'] != len(ref['lods'][section['assetLOD']]['vertices']):
                raise ValueError('Render/physical bounds inventory differs')
            for mapping in rows:
                if mapping['sourceIndices'][3] != 65535 and any(x >= row['physicalVertices'] for x in mapping['sourceIndices'][:3]):
                    raise ValueError('Actual render mapping exceeds physical vertices')
            render_records += len(rows)
        if not source_bounds: raise ValueError('No selected real render correspondence was verified')
    return dict(values=values, vertices=vertices, triangles=triangles, tethers=tethers, transitionRecords=records,
                nativeBytes=native_bytes, configValues=config_values, wrapComponents=wrap_components,
                maxWrapError=max_wrap_error, renderRecords=render_records, unavailableDefaultFields=unavailable,
                actualObject=actual['object'], referenceObject=ref['object'], pairing='explicit-byte-identical-owned-source-copy')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--output-dir', type=Path, required=True)
    args = parser.parse_args(); root = args.manifest.resolve().parent
    def path(value): return (root / value).resolve(strict=True)
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    cases = manifest['cases']
    if not isinstance(cases, list) or not 1 <= len(cases) <= 1000: raise ValueError('Select 1..1000 explicit cases')
    protected = {args.manifest.resolve(), args.consumer.resolve(), Path(__file__).resolve()}
    for row in cases:
        for key in ['container', 'global', 'context', 'reference', 'originalSource', 'sourceCopy']:
            protected.add(path(row[key]))
        container = path(row['container']); protected.update(container.parent.glob(container.stem + '*.ucas'))
        global_toc = path(row['global']); protected.update(global_toc.parent.glob(global_toc.stem + '*.ucas'))
        context = json.loads(path(row['context']).read_text(encoding='utf-8'))
        protected.add((path(row['context']).parent / context['usmap']).resolve(strict=True))
    before = {str(p): digest(p) for p in protected}
    out = args.output_dir.resolve()
    if out.exists() or any(p == out or p.is_relative_to(out) for p in protected): raise ValueError('Output must be a new independent report directory')
    out.mkdir(parents=True); env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}; result = []
    for i, row in enumerate(cases):
        if digest(path(row['originalSource'])) != row['sourceSha256'] or digest(path(row['sourceCopy'])) != row['sourceSha256']:
            raise ValueError('Declared source copy is not byte-identical to its original revision')
        reference = json.loads(path(row['reference']).read_text(encoding='utf-8'))
        cmd = [str(args.consumer.resolve()), str(path(row['container'])), row['entry'], str(path(row['global'])), str(path(row['context']))]
        if row.get('verifyRenderBounds', False):
            if type(row.get('keepMobileMinLOD')) is not bool or type(reference.get('hasVertexColors')) is not bool:
                raise ValueError('Supply explicit producer facts for optional render verification')
            cmd += ['--render-facts', str(int(row['keepMobileMinLOD'])), str(int(reference['hasVertexColors']))]
        p = subprocess.run(cmd, env=env, capture_output=True, timeout=180)
        if p.returncode: raise ValueError('Core consumer failed: ' + p.stderr.decode('utf-8', 'replace')[:1500])
        actual = json.loads(p.stdout); (out / (format(i, '03d') + '.json')).write_bytes(p.stdout)
        summary = compare(actual['before'], reference, row['actualMesh'], actual.get('beforeSourceBounds'))
        result.append(summary); print(row['entry'] + ': ' + str(summary['values']) + ' stored values; ' + str(summary['wrapComponents']) + ' wrap components', flush=True)
    if any(digest(Path(p)) != h for p, h in before.items()): raise ValueError('Protected input changed during validation')
    summary = {'schema': 'cyuediff.physical-cloth-geometry-validation/1', 'cases': result, 'inputSha256': before, 'inputsUnchanged': True}
    for key in ['values', 'vertices', 'triangles', 'tethers', 'transitionRecords', 'nativeBytes', 'configValues', 'wrapComponents', 'renderRecords']:
        summary[key] = sum(x[key] for x in result)
    summary['maxWrapError'] = max(x['maxWrapError'] for x in result)
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8', newline='\n')


if __name__ == '__main__': main()
