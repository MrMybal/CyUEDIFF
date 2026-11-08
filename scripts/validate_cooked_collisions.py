"""Validate available stored cooked collision geometry against loose inputs and an installed Core-only consumer.

This certifies selected reflected shape fields and body-local wire output. It does
not certify native physics blobs, enum-name resolution or omitted archetype defaults.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pairs', 'container', 'global-toc', 'schema', 'consumer', 'output-dir'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--cli', type=Path, default=Path('build-core/Release/cyuediff.exe'))
    parser.add_argument('--codec', type=Path)
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
    inputs.add((args.schema.parent/context['usmap']).resolve(strict=True))
    for toc in (args.container, args.global_toc):
        inputs.update(p.resolve() for p in toc.parent.glob(toc.stem+'*.ucas'))
    for pair in pairs:
        source = Path(pair['source']).resolve(strict=True)
        inputs.add(source)
        inputs.update(p for ext in ('.uexp','.ubulk','.uptnl','.upayload') if (p := source.with_suffix(ext)).is_file())
    if args.codec:
        inputs.add(args.codec.resolve(strict=True))
    if any(p.is_relative_to(out) for p in inputs):
        parser.error('Output overlaps an input')
    outputs = [out/'summary.json', *(out/f'{i:03}.json' for i in range(len(pairs)))]
    for target in outputs:
        if target.is_symlink():
            parser.error('Output reports must not be symbolic links')
        if target.exists() and any(os.path.samefile(target,p) for p in inputs):
            parser.error('Output aliases an input')
    out.mkdir(parents=True, exist_ok=True)
    before = {str(p): digest(p) for p in inputs}
    env = {k.upper() if os.name == 'nt' else k: v for k,v in os.environ.items()}
    def run(arguments):
        p = subprocess.run(list(map(str,arguments)), env=env, capture_output=True, timeout=120)
        if p.returncode:
            raise RuntimeError(p.stderr.decode('utf-8','replace'))
        return json.loads(p.stdout)
    def body(model):
        assert len(model['bodies']) == 1, 'Select pairs with one recognized collision body'
        return next(iter(model['bodies'].values()))
    def lines(wire):
        result = {}
        for shape in wire['shapes']:
            value = dict(shape)
            value.pop('body')
            result[shape['identity']] = value
        return result
    codec = ['--codec', args.codec] if args.codec else []
    common = ['--cooked-profile','ue5.3','--schema',args.schema,'--script-objects',args.global_toc,*codec]
    results = []
    try:
        for index,pair in enumerate(pairs):
            sdk = run([args.consumer,args.container,pair['id'],args.global_toc,args.schema,*([args.codec] if args.codec else [])])
            wire = sdk.pop('wireframe')
            cooked = run([args.cli,'zen-collision',args.container,pair['id'],*common])
            assert sdk == cooked, 'Installed Core/CLI mapped projection disagreement'
            cli_wire = run([args.cli,'zen-collision-wire',args.container,pair['id'],*common])
            cli_wire.pop('provenance')
            assert wire == cli_wire, 'Installed Core/CLI prepared wire disagreement'
            assert cooked['provenance']['nativePhysicsDecoded'] is False
            source = run([args.cli,'collision',pair['source']])
            source_wire = run([args.cli,'collision-preview',pair['source']])
            a,b = body(source)['shapes'],body(cooked)['shapes']
            assert a.keys() == b.keys(), 'Available stored shape identities differ'
            assert all(a[k]['geometry'] == b[k]['geometry'] for k in a), 'Stored geometry fields differ'
            assert lines(source_wire) == lines(wire), 'Body-local prepared geometry differs'
            edges = sum(len(s['lines']) for s in wire['shapes'])
            results.append({'package':pair['name'],'shapes':len(b),'geometryFields':sum(len(s['geometry']) for s in b.values()),
                            'edges':edges,'storedGeometryMatches':True,'nativePhysicsDecoded':False})
            (out/f'{index:03}.json').write_text(json.dumps({'collision':cooked,'wireframe':wire}),encoding='utf-8')
    finally:
        unchanged = all(digest(Path(p)) == h for p,h in before.items())
        summary = {'complete':len(results) == len(pairs) and unchanged,'packages':len(results),
                   'shapes':sum(r['shapes'] for r in results),'geometryFields':sum(r['geometryFields'] for r in results),
                   'edges':sum(r['edges'] for r in results),'nativePhysicsDecoded':False,'originalsUnchanged':unchanged,
                   'comparison':'available stored geometry and prepared body-local lines; no native physics/defaults/enum resolution',
                   'inputs':before,'results':results}
        (out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
        assert unchanged, 'Original inputs changed during validation'
    print(json.dumps({k:v for k,v in summary.items() if k not in ('inputs','results')},indent=2))


if __name__ == '__main__':
    main()
