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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--consumer', type=Path, required=True)
    parser.add_argument('--manifest', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    manifest_path = absolute(str(args.manifest.resolve()))
    manifest = load(manifest_path)
    if manifest['schema'] != 'cyuediff.nanite-geometry-validation/1' or not 1 <= len(manifest['cases']) <= 100:
        raise ValueError('Select 1..100 explicit page-set validation cases')
    protected = {manifest_path: digest(manifest_path)}
    for row in manifest.get('protectedSources', []):
        p = absolute(row['path'])
        if digest(p) != row['sha256']:
            raise ValueError('Protected source changed before validation')
        protected[p] = row['sha256']
    for row in manifest['cases']:
        for field in ('pages', 'triangleReference', 'sourceReference'):
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
        process = subprocess.run([str(args.consumer.resolve()), row['pages'], str(target)], capture_output=True, timeout=60)
        if process.returncode:
            raise ValueError(process.stderr.decode('utf-8', 'replace'))
        actual = load(target)
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
        results.append({'object': source['object'], 'pages': len(expected), 'clusters': catalog['clusters'],
                        'vertices': catalog['vertices'], 'positionCoordinates': coordinates,
                        'actualEngineTriangles': triangles, 'sourceOrientedTriangles': sum(leaves.values())})
    if any(digest(p) != h for p, h in protected.items()):
        raise ValueError('Protected validation input changed')
    summary = {'schema': 'cyuediff.nanite-geometry-validation-result/1', 'cases': results,
               'actualEngineTriangles': sum(r['actualEngineTriangles'] for r in results),
               'sourceOrientedTriangles': sum(r['sourceOrientedTriangles'] for r in results),
               'positionCoordinates': sum(r['positionCoordinates'] for r in results),
               'protectedInputs': len(protected), 'inputsUnchanged': True}
    (args.output/'summary.json').write_text(json.dumps(summary, indent=2)+'\n', encoding='utf-8')
    print(json.dumps(summary, indent=2))


if __name__ == '__main__':
    main()
