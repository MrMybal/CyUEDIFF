"""Compare explicitly paired cooked texture properties with their loose sources."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ('pairs', 'context', 'container', 'script-objects'):
        parser.add_argument('--'+name, type=Path, required=True)
    parser.add_argument('--codec', type=Path)
    parser.add_argument('--cli', type=Path, default=Path('build-native/Release/cyuediff.exe'))
    parser.add_argument('--output-dir', type=Path, default=Path('build-native/uht-validation'))
    parser.add_argument('--allow-unavailable', action='store_true', help='Record known opaque reads without accepting mismatched values')
    args = parser.parse_args()
    workspace = Path(__file__).resolve().parents[1]
    out = args.output_dir.resolve()
    if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
        parser.error('Reports must stay in an ignored workspace build directory')
    cli = args.cli.resolve(strict=True)
    pairs = json.loads(args.pairs.read_text(encoding='utf-8'))
    if not isinstance(pairs, list) or not 1 <= len(pairs) <= 1000:
        parser.error('Expected 1 through 1000 explicit texture pairs')
    sources = {args.pairs.resolve(strict=True), args.context.resolve(strict=True)}
    context = json.loads(args.context.read_text(encoding='utf-8'))
    mapping = Path(context['usmap'])
    sources.add((args.context.parent/mapping).resolve(strict=True) if not mapping.is_absolute() else mapping.resolve(strict=True))
    for pair in pairs: sources.add(Path(pair['source']).resolve(strict=True))
    def command(*arguments):
        result = subprocess.run([str(cli), *map(str, arguments)], capture_output=True, timeout=60)
        if result.returncode:
            raise RuntimeError(result.stderr.decode('utf-8', 'replace')[:1000])
        return json.loads(result.stdout)
    for path in (args.container, args.script_objects):
        inventory = command('container', path)
        sources.update(Path(value).resolve(strict=True) for value in inventory['files'])
    if args.codec: sources.add(args.codec.resolve(strict=True))
    if any(source.is_relative_to(out) or source.parent == out for source in sources):
        parser.error('Output directory contains an input')
    out.mkdir(parents=True, exist_ok=True)
    hashes = {str(path):digest(path) for path in sources}
    results = []
    try:
        for pair in pairs:
            source = Path(pair['source'])
            if source.suffix.lower() != '.uasset': raise ValueError('Expected a loose asset source')
            loose = command('inspect', source)
            textures = [obj for obj in loose['snapshot']['objects'].values() if obj['class']=='/Script/Engine.Texture2D']
            if len(textures) != 1: raise ValueError('Pair needs exactly one loose Texture2D')
            expected = {name: textures[0]['properties'][name]['value'] for name in ('LightingGuid','ImportedSize')
                        if name in textures[0]['properties'] and 'value' in textures[0]['properties'][name]}
            options = ['--cooked-profile','ue5.3','--script-objects',args.script_objects,'--schema',args.context]
            if args.codec: options.extend(['--codec',args.codec])
            cooked = command('zen-inspect', args.container, pair['id'], *options)
            if pair.get('sha256') and pair['sha256'] != cooked['sha256']:
                raise ValueError('Cooked chunk disagrees with the selected pair fingerprint')
            if pair.get('name') and pair['name'] != cooked['name']:
                raise ValueError('Cooked package identity disagrees with the selected pair')
            textures = [obj for obj in cooked['exports'] if obj['class']=='/Script/Engine.Texture2D']
            if len(textures) != 1: raise ValueError('Pair needs exactly one cooked Texture2D')
            read = textures[0]['propertyRead']
            row = {'source':str(source),'package':cooked['name'],'id':pair['id'],'status':read['status']}
            if read['status']=='decoded':
                actual = {}
                for name,value in read['properties'].items():
                    key = name.rsplit('.',1)[-1]
                    if key in expected:
                        if key in actual: raise ValueError('Ambiguous inherited comparison field')
                        actual[key] = value['value']
                row.update(consumedBytes=read['consumedBytes'],comparedFields=len(actual),match=actual==expected,
                           properties=len(read['properties']))
                if actual != expected: raise ValueError('Cooked/source named field mismatch')
            else:
                row.update(error=read.get('error'),message=read.get('message'))
            results.append(row)
    finally:
        unchanged = all(digest(Path(path))==value for path,value in hashes.items())
        report = {'pairs':len(pairs),'decoded':sum(row['status']=='decoded' for row in results),
                  'unavailable':sum(row['status']!='decoded' for row in results),
                  'matchedFields':sum(row.get('comparedFields',0) for row in results),
                  'propertiesDecoded':sum(row.get('properties',0) for row in results),
                  'originalsUnchanged':unchanged,'results':results,'sourceFingerprints':hashes}
        report['complete'] = len(results)==len(pairs) and report['unavailable']==0
        (out/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        if not unchanged: raise RuntimeError('An original input changed during validation')
    print(json.dumps({key:value for key,value in report.items() if key not in ('results','sourceFingerprints')},indent=2))
    return 0 if report['decoded'] and (report['complete'] or args.allow_unavailable) else 1


if __name__=='__main__':
    raise SystemExit(main())
