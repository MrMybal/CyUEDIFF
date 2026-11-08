"""Validate installed Core Nanite position/topology queries against explicit independent references."""
import argparse
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import subprocess
import struct


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def load(path):
    if path.stat().st_size > 128 * 1024 * 1024:
        raise ValueError('Validation input exceeds read budget')
    return json.loads(path.read_text(encoding='utf-8'))


def absolute(value):
    p = Path(value)
    if not p.is_absolute() or not p.is_file():
        raise ValueError('Use explicit absolute existing validation inputs')
    return p.resolve()


def oriented(points):
    return min(tuple(points[i:]+points[:i]) for i in range(3))


def validate_attributes(actual, catalog, row):
    if not row.get('attributeReference'):
        return 0, 0, 0.0
    reference = load(absolute(row['attributeReference']))
    if reference['schema'] != 'cyuediff.nanite-shader-attributes-reference/1' or reference['profile'] != 'ue5.3-windows':
        raise ValueError('Require an explicit matching native shader attribute reference')
    if not catalog['storedAttributesDecoded'] or len(reference['pages']) != len(actual['pages']):
        raise ValueError('Attribute coverage/page cardinality differs')
    if reference['pageHashes'] != [p['sha256'] for p in catalog['pages']]:
        raise ValueError('Shader reference is bound to different stored pages')
    vertices = uv_pairs = 0
    maximum_delta = 0.0
    def values(left, right, tolerance):
        nonlocal maximum_delta
        if right is None:
            if left is not None:
                raise ValueError('Absent/zero-bit shader channel was invented')
            return
        if not isinstance(left, list) or len(left) != len(right):
            raise ValueError('Shader channel cardinality differs')
        for a, b in zip(left, right):
            if isinstance(b, list):
                values(a, b, tolerance)
            else:
                if not math.isfinite(a) or not math.isfinite(b) or abs(a-b) > tolerance:
                    raise ValueError('Decoded attribute differs from actual shader reference')
                maximum_delta = max(maximum_delta, abs(a-b))
    for page, expected_page in zip(actual['pages'], reference['pages']):
        if len(page) != len(expected_page):
            raise ValueError('Shader cluster cardinality differs')
        for cluster, expected in zip(page, expected_page):
            attributes = cluster.get('attributes', [])
            if not cluster['metadata']['storedAttributesDecoded'] or len(attributes) != cluster['metadata']['vertices'] or len(attributes) != len(expected):
                raise ValueError('Shader vertex cardinality/coverage differs')
            for actual_vertex, expected_vertex in zip(attributes, expected):
                for field in ('packedNormal', 'packedTangent', 'normalPrecision', 'tangentPrecision', 'color', 'packedUVs'):
                    if actual_vertex[field] != expected_vertex[field]:
                        raise ValueError('Packed attribute differs from actual shader transcode')
                values(actual_vertex['normal'], expected_vertex['normal'], 3e-6)
                values(actual_vertex['tangent'], expected_vertex['tangent'], 3e-6)
                values(actual_vertex['uvs'], expected_vertex['uvs'], 0.0)
                vertices += 1
                uv_pairs += len(actual_vertex['uvs'])
    return vertices, uv_pairs, maximum_delta


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest_path = absolute(str(args.manifest.resolve()))
    manifest = load(manifest_path)
    cooked = manifest['schema'] == 'cyuediff.cooked-nanite-validation/1'
    if manifest['schema'] not in ('cyuediff.nanite-geometry-validation/1', 'cyuediff.cooked-nanite-validation/1') or not 1 <= len(manifest['cases']) <= 100:
        raise ValueError('Select 1..100 explicit page-set validation cases')
    protected = {manifest_path: digest(manifest_path)}
    for row in manifest.get('protectedSources', []):
        p = absolute(row['path'])
        if digest(p) != row['sha256']:
            raise ValueError('Protected source changed before validation')
        protected[p] = row['sha256']
    for row in manifest['cases']:
        fields = ('pages', 'triangleReference', 'sourceReference') + (('container', 'global', 'context', 'packageReference') if cooked else ())
        if row.get('codec'):
            fields += ('codec',)
        if row.get('attributeReference'):
            fields += ('attributeReference',)
        for field in fields:
            p = absolute(row[field])
            protected[p] = digest(p)
        for page in load(absolute(row['pages']))['pages']:
            p = absolute(page['file'])
            protected[p] = digest(p)
    args.output.mkdir(parents=True, exist_ok=False)
    results = []
    for index, row in enumerate(manifest['cases']):
        source = load(absolute(row['sourceReference']))
        if source['schema'] != 'cyuediff.unreal-nanite-source-reference/1' or not source['engine'].startswith('5.3.'):
            raise ValueError('Require matching actual source MeshDescription API reference')
        expected = load(absolute(row['triangleReference']))
        target = args.output/f'{index}-actual.json'
        if cooked:
            if type(row['mobileMinLod']) is not bool:
                raise ValueError('Explicit producer mobile LOD boolean required')
            command = [str(args.consumer.resolve()), row['container'], row['entry'], row['global'], row['context'], '1' if row['mobileMinLod'] else '0', str(target)]
            if row.get('codec'):
                command.append(row['codec'])
        else:
            command = [str(args.consumer.resolve()), row['pages'], str(target)]
        process = subprocess.run(command, capture_output=True, timeout=60)
        if process.returncode:
            raise ValueError(process.stderr.decode('utf-8', 'replace'))
        actual = load(target)
        if cooked:
            info = actual['nanite']
            page_manifest = load(absolute(row['pages']))
            if info['status'] != 'decoded' or not info['geometryKnown'] or info['source']['object'] != source['object'] or info['source']['packageSha256'] != digest(absolute(row['packageReference'])):
                raise ValueError('Whole-package identity or native coverage differs')
            if info['rootPages'] != page_manifest['rootPages'] or len(info['pages']) != len(page_manifest['pages']):
                raise ValueError('Whole-package root/page declarations differ')
            for native, expected_page in zip(info['pages'], page_manifest['pages']):
                if native['sha256'] != digest(absolute(expected_page['file'])) or native['flags'] != expected_page['flags'] or native['dependencies'] != expected_page['dependencies']:
                    raise ValueError('Whole cooked page bytes/identities/dependencies differ')
            catalog = info['geometry']
        else:
            catalog = actual['catalog']
        if len(actual['pages']) != len(expected) or catalog['clusters'] != source['naniteClusters'] or len(actual['pages']) != source['nanitePages']:
            raise ValueError('Actual source resource/page cardinality differs')
        source_vertices = {v['id']: tuple(v['quantizedPosition']) for v in source['vertices']}
        if len(source_vertices) != len(source['vertices']):
            raise ValueError('Ambiguous source vertex IDs')
        source_triangles = Counter(oriented([source_vertices[v] for v in tri]) for tri in source['triangles'])
        leaves = Counter()
        coordinates = triangles = 0
        for pi, (page, reference_page) in enumerate(zip(actual['pages'], expected)):
            if len(page) != len(reference_page):
                raise ValueError('Actual Engine cluster cardinality differs')
            for cluster, reference in zip(page, reference_page):
                meta = cluster['metadata']
                if meta['positionPrecision'] != source['positionPrecision'] or len(cluster['positions']) != meta['vertices'] or len(cluster['quantizedPositions']) != meta['vertices']:
                    raise ValueError('Position query cardinality/precision differs')
                if [t['vertices'] for t in cluster['triangles']] != reference:
                    raise ValueError('Oriented triangle indices differ from actual Engine decoder')
                triangles += len(reference)
                for position, quantized in zip(cluster['positions'], cluster['quantizedPositions']):
                    for value, integer in zip(position, quantized):
                        if not math.isfinite(value) or value != struct.unpack('<f', struct.pack('<f', math.ldexp(struct.unpack('<f', struct.pack('<f', integer))[0], -meta['positionPrecision'])))[0]:
                            raise ValueError('Integer/float position queries disagree')
                        coordinates += 1
                if meta['fullLeaf']:
                    for triangle in cluster['triangles']:
                        leaves[oriented([tuple(cluster['quantizedPositions'][v]) for v in triangle['vertices']])] += 1
        if leaves != source_triangles:
            raise ValueError('Full-leaf quantized oriented triangle positions differ from actual source API')
        shader_vertices, shader_uvs, shader_delta = validate_attributes(actual, catalog, row)
        results.append({'object': source['object'], 'pages': len(expected), 'clusters': catalog['clusters'],
                        'vertices': catalog['vertices'], 'positionCoordinates': coordinates,
                        'actualEngineTriangles': triangles, 'sourceOrientedTriangles': sum(leaves.values()),
                        'shaderAttributeVertices': shader_vertices, 'shaderUVPairs': shader_uvs,
                        'maximumShaderChannelDelta': shader_delta})
    if any(digest(p) != h for p, h in protected.items()):
        raise ValueError('Protected validation input changed')
    summary = {'schema': 'cyuediff.cooked-nanite-validation-result/1' if cooked else 'cyuediff.nanite-geometry-validation-result/1', 'cases': results,
               'actualEngineTriangles': sum(r['actualEngineTriangles'] for r in results),
               'sourceOrientedTriangles': sum(r['sourceOrientedTriangles'] for r in results),
               'positionCoordinates': sum(r['positionCoordinates'] for r in results),
               'shaderAttributeVertices': sum(r['shaderAttributeVertices'] for r in results),
               'shaderUVPairs': sum(r['shaderUVPairs'] for r in results),
               'maximumShaderChannelDelta': max(r['maximumShaderChannelDelta'] for r in results),
               'protectedInputs': len(protected), 'inputsUnchanged': True}
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
