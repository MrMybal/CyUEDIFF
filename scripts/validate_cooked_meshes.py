"""Validate explicit cooked/static-source pairs through CLI and an installed Core-only consumer.

Oriented triangle positions and material groups must match LOD 0 of the source.
Other LODs receive public CLI/SDK parity checks; no source-LOD correspondence is inferred.
Host-owned assets, native mappings, codecs and reports stay outside published sources.
"""
import argparse
import collections
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def triangles(geometry):
    vertices = {k: tuple(v['position']) for k, v in geometry['vertices'].items()}
    instances = {k: v['vertex'] for k, v in geometry['instances'].items()}
    result = []
    for triangle in geometry['triangles'].values():
        points = tuple(vertices[str(instances[str(i)])] for i in triangle['instances'])
        result.append((triangle['group'], min(points, points[1:]+points[:1], points[2:]+points[:2])))
    return set(vertices.values()), collections.Counter(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for key in ('pairs', 'container', 'global-toc', 'schema', 'consumer', 'output-dir'):
        parser.add_argument('--'+key, type=Path, required=True)
    parser.add_argument('--keep-mobile-min-lod', choices=('0', '1'), required=True)
    parser.add_argument('--codec', type=Path)
    parser.add_argument('--cli', type=Path, default=Path('build-core/Release/cyuediff.exe'))
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
        parser.error('Reports must stay in an ignored workspace build directory')
    pairs = json.loads(args.pairs.read_text(encoding='utf-8'))
    if not isinstance(pairs, list) or not 1 <= len(pairs) <= 1000:
        parser.error('Expected 1 through 1000 explicit pairs')
    inputs = {p.resolve(strict=True) for p in (args.pairs, args.container, args.global_toc, args.schema, args.consumer, args.cli)}
    context = json.loads(args.schema.read_text(encoding='utf-8'))
    inputs.add((args.schema.parent / context['usmap']).resolve(strict=True))
    for toc in (args.container, args.global_toc):
        # Include every partition for the selected stem, without treating unrelated files as inputs.
        inputs.update(p.resolve() for p in toc.parent.glob(toc.stem+'*.ucas'))
    for pair in pairs:
        source = Path(pair['source']).resolve(strict=True)
        inputs.add(source)
        inputs.update(p for suffix in ('.uexp', '.ubulk', '.uptnl', '.upayload') if (p := source.with_suffix(suffix)).is_file())
    if args.codec:
        inputs.add(args.codec.resolve(strict=True))
    if any(p.is_relative_to(out) for p in inputs):
        parser.error('Output overlaps an input')
    out.mkdir(parents=True, exist_ok=True)
    before = {str(p): digest(p) for p in inputs}
    env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}
    def run(command):
        p = subprocess.run(list(map(str, command)), env=env, capture_output=True, timeout=120)
        if p.returncode:
            raise RuntimeError(p.stderr.decode('utf-8', 'replace'))
        return json.loads(p.stdout)
    codec = ['--codec', args.codec] if args.codec else []
    common = ['--cooked-profile', 'ue5.3', '--mesh-profile', 'ue5.3-windows', '--keep-mobile-min-lod', args.keep_mobile_min_lod,
              '--script-objects', args.global_toc, '--schema', args.schema, *codec]
    results = []
    try:
        for index, pair in enumerate(pairs):
            sdk = run([args.consumer, args.container, pair['id'], args.global_toc, args.schema, args.keep_mobile_min_lod,
                       *([args.codec] if args.codec else [])])
            assert sdk['schema'] == 'cyuediff.cooked-static-mesh/1'
            assert sdk['tail']['decoded'] is False
            source = run([args.cli, 'geometry', pair['source'], *codec])
            first = None
            for lod_index, lod in enumerate(sdk['lods']):
                if not lod['geometryAvailable']:
                    raise AssertionError(f'Unvalidated unavailable LOD {pair["name"]} {lod_index}')
                cli = run([args.cli, 'zen-geometry', args.container, pair['id'], '--lod', lod_index, *common])
                native = cli.pop('cookedMesh')
                assert cli.pop('lod') == lod_index
                assert cli == lod['geometry'], 'Installed Core/CLI geometry disagreement'
                assert native['packageSha256'] == sdk['packageSha256'] and native['tail'] == sdk['tail']
                if lod_index == 0:
                    first = cli
            assert first is not None
            cooked_positions, cooked_triangles = triangles(first)
            source_positions, source_triangles = triangles(source)
            assert cooked_triangles == source_triangles, f'Oriented material triangle disagreement: {pair["name"]}'
            assert cooked_positions <= source_positions, f'Render position absent from source: {pair["name"]}'
            (out/f'{index:03}.json').write_text(json.dumps(sdk), encoding='utf-8')
            results.append({'package': pair['name'], 'id': pair['id'], 'lods': len(sdk['lods']),
                            'sourceTriangles': sum(source_triangles.values()), 'renderVertices': len(first['vertices']),
                            'orientedMaterialTrianglesMatch': True, 'renderPositionsInSource': True,
                            'sourceUnusedPositionCount': len(source_positions-cooked_positions)})
    finally:
        unchanged = all(digest(Path(p)) == h for p, h in before.items())
        summary = {'complete': len(results) == len(pairs) and unchanged, 'packages': len(results),
                   'lods': sum(r['lods'] for r in results), 'sourceTriangles': sum(r['sourceTriangles'] for r in results),
                   'originalsUnchanged': unchanged, 'inputs': before, 'results': results}
        (out/'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        assert unchanged, 'Original inputs changed during validation'
    print(json.dumps({k: v for k, v in summary.items() if k not in ('inputs', 'results')}, indent=2))


if __name__ == '__main__':
    main()
