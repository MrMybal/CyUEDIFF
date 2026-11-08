"""Compare all common supported mapped property leaves with explicit loose source pairs.

Reports and private data stay in an ignored build directory. Object/reference
identity and resolved per-platform values require different comparison domains and
are excluded explicitly; missing archetype defaults are never invented. A zero
mismatch result certifies these selected common values, not every native serializer.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

_SKIP = object()
_REFERENCE_TYPES = {
    'ObjectProperty', 'ClassProperty', 'SoftObjectProperty', 'SoftClassProperty',
    'AssetObjectProperty', 'WeakObjectProperty', 'LazyObjectProperty',
    'InterfaceProperty', 'DelegateProperty', 'MulticastDelegateProperty'
}


def is_property(value):
    return isinstance(value, dict) and 'type' in value and 'value' in value and (
        isinstance(value['type'], str) or isinstance(value['type'], dict) and
        isinstance(value['type'].get('name'), str))


def norm(value):
    if isinstance(value, dict):
        if is_property(value):
            kind = value['type'] if isinstance(value['type'], str) else value['type']['name']
            if kind in _REFERENCE_TYPES or value.get('nativeLayout', '').startswith('per-platform-'):
                return _SKIP
            result = norm(value['value'])
            return _SKIP if result is _SKIP else {'$type': kind, '$value': result}
        fields = bool(value) and all(is_property(child) for child in value.values())
        result = {}
        for key, child in value.items():
            normalized = norm(child)
            if normalized is _SKIP:
                continue
            name = key.rsplit('.', 1)[-1] if fields else key
            if fields and name.endswith('[0]'):
                name = name[:-3]
            if name in result:
                raise ValueError('Ambiguous normalized property identity: ' + name)
            result[name] = normalized
        return result
    if isinstance(value, list):
        result = [norm(child) for child in value]
        return _SKIP if any(child is _SKIP for child in result) else result
    return value


def flat(value, path=''):
    if isinstance(value, dict):
        return {key: leaf for name, child in value.items()
                for key, leaf in flat(child, path+'/'+name).items()}
    if isinstance(value, list):
        return {path+'/$length': len(value), **{
            key: leaf for index, child in enumerate(value)
            for key, leaf in flat(child, path+'/'+str(index)).items()}}
    return {path: value}


def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream,'sha256').hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('pairs','context','container','script-objects'):parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--codec',type=Path)
    parser.add_argument('--cli',type=Path,default=Path('build-core/Release/cyuediff.exe'))
    parser.add_argument('--output-dir',type=Path,default=Path('build-goal/mapped-values-validation'))
    args=parser.parse_args()
    workspace=Path(__file__).resolve().parents[1];out=args.output_dir.resolve()
    if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
        parser.error('Reports must stay in an ignored workspace build directory')
    pairs=json.loads(args.pairs.read_text(encoding='utf-8'))
    if not isinstance(pairs,list) or not 1<=len(pairs)<=1000:parser.error('Expected 1 through 1000 explicit source pairs')
    cli=str(args.cli.resolve(strict=True));env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
    options=['--cooked-profile','ue5.3','--schema',str(args.context),'--script-objects',str(args.script_objects)]
    if args.codec:options+=['--codec',str(args.codec)]
    def command(*arguments):
        p=subprocess.run([cli,*map(str,arguments)],env=env,capture_output=True,timeout=120)
        assert p.returncode==0,p.stderr.decode('utf-8','replace')
        return json.loads(p.stdout)
    inputs={args.pairs.resolve(strict=True),args.context.resolve(strict=True)}
    context=json.loads(args.context.read_text(encoding='utf-8'));mapping=Path(context['usmap'])
    inputs.add((mapping if mapping.is_absolute() else args.context.parent/mapping).resolve(strict=True))
    for path in (args.container,args.script_objects):inputs.update(Path(p).resolve(strict=True) for p in command('container',path)['files'])
    if args.codec:inputs.add(args.codec.resolve(strict=True))
    inputs.update(Path(p['source']).resolve(strict=True) for p in pairs)
    inputs.update(companion for source in list(inputs) if source.suffix.lower()=='.uasset'
        for extension in ('.uexp','.ubulk','.uptnl','.upayload')
        if (companion:=source.with_suffix(extension)).is_file())
    if any(p.is_relative_to(out) or p.parent==out for p in inputs):parser.error('Output overlaps an input')
    out.mkdir(parents=True,exist_ok=True);before={str(p):digest(p) for p in inputs}
    results=[];mismatches=[];matched=properties=0
    try:
        for pair in pairs:
            source=command('inspect',pair['source'])
            loose=[x for x in source['snapshot']['objects'].values() if x['class']==pair['class']]
            assert len(loose)==1,'Choose a source with one object of the specified class'
            cooked=command('zen-inspect',args.container,pair['id'],*options)
            assert cooked['name']==pair['name']
            if pair.get('sha256'):assert cooked['sha256']==pair['sha256']
            objects=[e for e in cooked['exports'] if e['class']==pair['class'] and not e['flags']&0x30]
            assert len(objects)==1,'Choose a cooked package with one non-template object of the specified class'
            read=objects[0]['propertyRead'];assert read['status']=='decoded',read
            properties+=len(read['properties'])
            a=flat(norm(loose[0]['properties']));b=flat(norm(read['properties']))
            common=sorted(a.keys()&b.keys());assert common,'No comparable stored property leaves'
            differences=[{'field':k,'source':a[k],'cooked':b[k]} for k in common if a[k]!=b[k]]
            matched+=len(common)-len(differences)
            mismatches.extend({'package':pair['name'],**d} for d in differences)
            results.append({'package':pair['name'],'sha256':cooked['sha256'],'consumedBytes':read['consumedBytes'],
                'presentProperties':len(read['properties']),'comparedLeaves':len(common),'differences':differences})
    finally:
        unchanged=all(digest(Path(path))==value for path,value in before.items())
        summary={'complete':len(results)==len(pairs) and not mismatches and unchanged,
            'packages':len(results),'presentProperties':properties,'matchedLeaves':matched,'mismatches':mismatches,
            'originalsUnchanged':unchanged,'inputs':before,'results':results,
            'comparison':'common stored typed leaves; references, per-platform resolution and unstored defaults excluded'}
        (out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
        assert unchanged,'Original inputs changed during validation'
    print(json.dumps({k:v for k,v in summary.items() if k not in ('inputs','results','mismatches')},indent=2))
    if mismatches:raise RuntimeError(str(len(mismatches))+' common mapped values differ; inspect the private report')

if __name__=='__main__':main()
