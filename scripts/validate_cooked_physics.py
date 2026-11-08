"""Validate explicit UE 5.3 native Chaos streams with an independent Python wire reader.

Reports and extracted private chunks remain in an ignored workspace build directory.
The independent reader covers wire fields, not physics simulation or plugin serializers.
"""
import argparse
import os
import subprocess
from pathlib import Path
import hashlib
import json
import struct

class Reference:
    """Independent selected-profile wire reader; never executes engine serializers."""

    def __init__(self, b):
        self.b = b
        self.p = 0
        self.tags = {}

    def read(self, f):
        n = struct.calcsize('<' + f)
        assert self.p + n <= len(self.b), (self.p, n, len(self.b))
        v = struct.unpack_from('<' + f, self.b, self.p)
        self.p += n
        return v[0] if len(v) == 1 else list(v)

    def count(self):
        n = self.read('i')
        assert 0 <= n <= 1000000, (self.p, n)
        return n

    def arr(self, f):
        return [self.read(f) for _ in range(self.count())]

    def boolean(self):
        n = self.read('i')
        assert n in (0, 1), (self.p, n)
        return bool(n)

    def convex(self):
        start = self.p
        kind = self.read('B')
        assert kind == 8, kind
        out = {'type': kind, 'kind': 'convex' if kind == 8 else 'triangleMesh', 'isConvex': self.boolean(), 'collide': self.boolean(), 'collisionType': self.read('B')}
        out['planes'] = self.arr('6f')
        out['vertices'] = self.arr('3f')
        out['bounds'] = self.read('6f')
        out['volume'] = self.read('f')
        out['centerOfMass'] = self.read('3f')
        out['margin'] = self.read('f')
        k = self.read('B')
        out['structureIndexType'] = k
        if k:
            f = {1: 'B', 2: 'h', 3: 'i'}[k]
            out['faces'] = self.arr(f * 2)
            out['halfEdges'] = self.arr(f * 3)
            out['vertexEdges'] = self.arr(f)
            out['uniqueEdges'] = self.arr(f)
        out['unitMassInertiaTensor'] = self.read('3f')
        out['rotationOfMass'] = self.read('4d')
        out['offset'] = start
        out['bytes'] = self.p - start
        return out

    def trimesh(self):
        start = self.p
        kind = self.read('B')
        assert kind == 11, (self.p, kind)
        out = {'type': kind, 'kind': 'convex' if kind == 8 else 'triangleMesh', 'isConvex': self.boolean(), 'collide': self.boolean(), 'collisionType': self.read('B')}
        assert self.boolean()
        out['vertices'] = self.arr('3f')
        wide = self.boolean()
        out['indexBits'] = 32 if wide else 16
        out['triangles'] = self.arr('3i' if wide else '3H')
        out['bounds'] = self.read('6f')
        out['bvhNodes'] = [[{'bounds': row[:6], 'index': row[6], 'faces': row[7]}, {'bounds': row[8:14], 'index': row[14], 'faces': row[15]}] for row in self.arr('6fii6fii')]
        out['faceBounds'] = self.arr('6f')
        out['materials'] = self.arr('H')
        out['externalFaces'] = self.arr('i')
        out['externalVertices'] = self.arr('i')
        out['offset'] = start
        out['bytes'] = self.p - start
        return out

    def ptr(self, reader):
        if not self.boolean():
            return None
        tag = self.read('i')
        assert 0 <= tag <= 100000
        if tag not in self.tags:
            self.tags[tag] = reader()
            self.tags[tag]['tag'] = tag
        return tag

    def decode(self):
        assert self.read('i') == 4
        out = {'schema': 'cyuediff.chaos-physics/1', 'profile': 'ue5.3-windows', 'precisionBytes': 4, 'bytes': len(self.b), 'sha256': hashlib.sha256(self.b).hexdigest(), 'convexRefs': [self.ptr(self.convex) for _ in range(self.count())]}
        out['triangleMeshRefs'] = [self.ptr(self.trimesh) for _ in range(self.count())]
        out['objects'] = list(self.tags.values())
        out['uvIndices'] = self.arr('i')
        out['uvPositions'] = self.arr('3d')
        out['uvChannels'] = [self.arr('2d') for _ in range(self.count())]
        out['faceRemap'] = self.arr('i')
        assert self.p == len(self.b), (self.p, len(self.b))
        return out

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pairs', 'container', 'global-toc', 'schema', 'consumer', 'output-dir'):
        parser.add_argument('--' + name, type=Path, required=True)
    parser.add_argument('--cli', type=Path, default=Path('build-core/Release/cyuediff.exe'))
    parser.add_argument('--codec', type=Path)
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts or not out.relative_to(workspace).parts[0].startswith('build'):
        parser.error('Reports must stay in an ignored workspace build directory')
    pairs = json.loads(args.pairs.read_text(encoding='utf-8'))
    if not isinstance(pairs, list) or not 1 <= len(pairs) <= 1000:
        parser.error('Expected 1 through 1000 explicit pairs')
    inputs = {p.resolve(strict=True) for p in (args.pairs, args.container, args.global_toc, args.schema, args.consumer, args.cli)}
    context = json.loads(args.schema.read_text(encoding='utf-8'))
    inputs.add((args.schema.parent / context['usmap']).resolve(strict=True))
    for toc in (args.container, args.global_toc):
        inputs.update((p.resolve() for p in toc.parent.glob(toc.stem + '*.ucas')))
    for pair in pairs:
        if 'source' in pair:
            source = Path(pair['source']).resolve(strict=True)
            inputs.add(source)
            inputs.update((p for ext in ('.uexp', '.ubulk', '.uptnl', '.upayload') if (p := source.with_suffix(ext)).is_file()))
    if args.codec:
        inputs.add(args.codec.resolve(strict=True))
    if any((p.is_relative_to(out) for p in inputs)):
        parser.error('Output overlaps an input')
    targets = [out / 'summary.json', *(out / f'{i:03}{ext}' for i in range(len(pairs)) for ext in ('.json', '.bin'))]
    for target in targets:
        if target.is_symlink():
            parser.error('Output reports must not be symbolic links')
        if target.exists() and any((os.path.samefile(target, p) for p in inputs)):
            parser.error('Output aliases an input')
    out.mkdir(parents=True, exist_ok=True)
    before = {str(p): digest(p) for p in inputs}
    env = {k.upper() if os.name == 'nt' else k: v for k, v in os.environ.items()}

    def run(arguments):
        p = subprocess.run(list(map(str, arguments)), env=env, capture_output=True, timeout=120)
        if p.returncode:
            raise RuntimeError(p.stderr.decode('utf-8', 'replace'))
        return json.loads(p.stdout) if p.stdout else None
    codec = ['--codec', args.codec] if args.codec else []
    common = ['--cooked-profile', 'ue5.3', '--schema', args.schema, '--script-objects', args.global_toc, *codec]
    results = []
    try:
        for i, pair in enumerate(pairs):
            sdk = run([args.consumer, args.container, pair['id'], args.global_toc, args.schema, *([args.codec] if args.codec else [])])
            native = run([args.cli, 'zen-physics', args.container, pair['id'], '--physics-profile', 'ue5.3-windows', *common])
            assert sdk == native, 'Installed SDK/CLI native physics disagreement'
            assert native['complete'] and native['bodyTableKnown']
            zen = run([args.cli, 'zen-inspect', args.container, pair['id'], *common])
            raw = out / f'{i:03}.bin'
            run([args.cli, 'container-read', args.container, pair['id'], *codec, '--out', raw])
            data = raw.read_bytes()
            counts = {'convexes': 0, 'triangleMeshes': 0, 'triangles': 0, 'vertices': 0, 'formats': 0}
            for body in native['bodies']:
                e = zen['exports'][body['exportIndex']]
                start = e['offset'] + e['propertyRead']['offset'] + e['propertyRead']['consumedBytes']
                end = e['offset'] + e['size']
                r = Reference(data[start:end])
                guid = r.read('16s').hex() if r.boolean() else None
                assert body['objectGuidBytes'] == guid and body['bodyGuidBytes'] == r.read('16s').hex() and r.boolean()
                assert body['hasCookedCollisionData'] == r.boolean()
                count = r.count()
                assert count == len(body['formats'])
                for fmt in body['formats']:
                    name, number = r.read('ii')
                    bulk_index = r.read('I')
                    bulk = zen['bulkData'][bulk_index]
                    assert fmt['name'] == zen['names'][name] and fmt['number'] == number and (fmt['bulkIndex'] == bulk_index)
                    # Selected real streams are inline; external storage has original fixtures.
                    assert bulk['flags'] & 64 and (not bulk['flags'] & 1) and (fmt['status'] == 'decoded')
                    size = bulk['size']
                    blob = r.b[r.p:r.p + size]
                    r.p += size
                    reference = Reference(blob).decode()
                    assert reference == fmt['physics'], 'Independent native wire field disagreement'
                    counts['formats'] += 1
                    for obj in reference['objects']:
                        counts['vertices'] += len(obj['vertices'])
                        if obj['kind'] == 'convex':
                            counts['convexes'] += 1
                        else:
                            counts['triangleMeshes'] += 1
                            counts['triangles'] += len(obj['triangles'])
                assert r.p == len(r.b), 'Independent BodySetup export boundary disagreement'
            results.append({'package': pair['name'], **counts, 'independentWireMatches': True, 'sdkMatches': True})
            (out / f'{i:03}.json').write_text(json.dumps(native), encoding='utf-8')
    finally:
        unchanged = all((digest(Path(p)) == h for p, h in before.items()))
        summary = {'complete': len(results) == len(pairs) and unchanged, 'packages': len(results), **{k: sum((x[k] for x in results)) for k in ('formats', 'convexes', 'triangleMeshes', 'triangles', 'vertices')}, 'originalsUnchanged': unchanged, 'comparison': 'exact independent wire fields and installed Core/CLI parity; no simulation/default reconstruction', 'inputs': before, 'results': results}
        (out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
        assert unchanged, 'Original inputs changed during validation'
    print(json.dumps({k: v for k, v in summary.items() if k not in ('inputs', 'results')}, indent=2))
if __name__ == '__main__':
    main()
