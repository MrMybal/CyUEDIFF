"""Validate explicit host-owned mesh source pairs through CLI and a Core-only SDK consumer.

The consumer must be built from examples/mesh_source_consumer: it requires the
stored full raw BLAKE3 before returning geometry. This validates public integration
and payload integrity; independent geometry-format evidence is tracked separately.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pairs',type=Path,required=True)
    parser.add_argument('--consumer',type=Path,required=True)
    parser.add_argument('--codec',type=Path)
    parser.add_argument('--cli',type=Path,default=Path('build-core/Release/cyuediff.exe'))
    parser.add_argument('--output-dir',type=Path,default=Path('build-goal/mesh-source-validation'))
    args=parser.parse_args()
    workspace=Path(__file__).resolve().parents[1];out=args.output_dir.resolve()
    if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
        parser.error('Reports must stay in an ignored workspace build directory')
    pairs=json.loads(args.pairs.read_text(encoding='utf-8'))
    if not isinstance(pairs,list) or not 1<=len(pairs)<=1000:parser.error('Expected 1 through 1000 explicit source pairs')
    consumer=args.consumer.resolve(strict=True);cli=args.cli.resolve(strict=True)
    inputs={args.pairs.resolve(strict=True),consumer,cli}
    inputs.update(Path(p['source']).resolve(strict=True) for p in pairs)
    for source in list(inputs):
        if source.suffix.lower()=='.uasset':
            inputs.update(companion for extension in ('.uexp','.ubulk','.uptnl','.upayload')
                if (companion:=source.with_suffix(extension)).is_file())
    codec=[]
    if args.codec:codec=[str(args.codec.resolve(strict=True))];inputs.add(Path(codec[0]))
    if any(path.is_relative_to(out) or path.parent==out for path in inputs):parser.error('Output overlaps an input')
    out.mkdir(parents=True,exist_ok=True);before={str(path):digest(path) for path in inputs}
    env={key.upper() if os.name=='nt' else key:value for key,value in os.environ.items()}
    results=[]
    try:
        for pair in pairs:
            sample=Path(pair['source']).resolve(strict=True)
            result=subprocess.run([str(consumer),str(sample),*codec],env=env,capture_output=True,timeout=120)
            assert result.returncode==0,result.stderr.decode('utf-8','replace')
            sdk=json.loads(result.stdout)
            assert sdk['schema']=='cyuediff.mesh-source-example/1' and sdk['rawHashVerified']
            options=['--codec',*codec] if codec else []
            result=subprocess.run([str(cli),'geometry',str(sample),*options],env=env,capture_output=True,timeout=120)
            assert result.returncode==0,result.stderr.decode('utf-8','replace')
            geometry=json.loads(result.stdout);assert geometry==sdk['geometry']
            results.append({'package':pair.get('name',sample.stem),'object':sdk['object'],
                'rawHashVerified':True,'contentId':sdk['contentId'],
                'vertices':len(geometry['vertices']),'instances':len(geometry['instances']),
                'triangles':len(geometry['triangles']),
                'geometrySha256':hashlib.sha256(json.dumps(geometry,sort_keys=True,separators=(',',':')).encode('utf-8')).hexdigest()})
    finally:
        unchanged=all(digest(Path(path))==value for path,value in before.items())
        summary={'complete':len(results)==len(pairs) and unchanged,'meshes':len(results),
            'triangles':sum(row['triangles'] for row in results),'originalsUnchanged':unchanged,
            'comparison':'public CLI/installed Core parity and full raw payload BLAKE3',
            'inputs':before,'results':results}
        (out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
        assert unchanged,'Original inputs changed during validation'
    print(json.dumps({k:v for k,v in summary.items() if k not in ('inputs','results')},indent=2))

if __name__=='__main__':main()
