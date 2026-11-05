"""Validate JSON/compact deltas and optional chains with an independent byte interpreter."""
import argparse,hashlib,json,os,subprocess,tempfile,time
from pathlib import Path
from binary_delta_reference import decode,reconstruct
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--exe',type=Path,default=Path('build-native/Release/cyuediff.exe'))
p.add_argument('--input',type=Path,action='append',required=True)
p.add_argument('--consumer',type=Path,help='Optional independently built installed Core consumer')
p.add_argument('--output-dir',type=Path)
p.add_argument('--format',choices=['json','binary'],default='json')
p.add_argument('--compose',action='store_true',help='Additionally validate A->B->C and composed A->C')
a=p.parse_args();workspace=Path(__file__).resolve().parents[1];exe=a.exe.resolve(strict=True)
inputs=list(dict.fromkeys(x.resolve(strict=True) for x in a.input))
if any(not x.is_file() or x.stat().st_size>256*1024*1024 for x in inputs):p.error('Inputs must be regular files of at most 256 MiB')
if a.output_dir:
 out=a.output_dir.resolve();relative=out.relative_to(workspace)
 if not relative.parts or not relative.parts[0].startswith('build'):p.error('Use a new ignored build output directory')
 if out.exists():p.error('Output directory already exists; choose a new directory')
 out.mkdir(parents=True)
else:
 (workspace/'build-native').mkdir(exist_ok=True);out=Path(tempfile.mkdtemp(prefix='binary-validation-',dir=workspace/'build-native'))
protected=set(inputs)
for path in inputs:
 for extension in ['.uexp','.ubulk','.uptnl','.upayload','.m.ubulk','.o.ubulk']:
  companion=path.with_suffix(extension)
  if companion.is_file():protected.add(companion.resolve())
hashes={str(path):hashlib.sha256(path.read_bytes()).hexdigest() for path in protected}
consumer=a.consumer.resolve(strict=True) if a.consumer else None
cases=[]
def run(arguments,expected):
 result=subprocess.run([str(exe),*map(str,arguments)],capture_output=True,timeout=120)
 if result.returncode!=expected:raise RuntimeError((arguments,result.returncode,result.stderr.decode('utf-8','replace')))
 return json.loads(result.stdout) if result.stdout else None
def verify(base,target,name,kind,chain=None):
 before,after=base.read_bytes(),target.read_bytes();delta=out/(name+'.delta');rebuilt=out/(name+'.rebuilt.bin')
 started=time.perf_counter()
 if chain:run(['binary-compose',*chain,'--format',a.format,'--out',delta],0)
 else:run(['binary-diff',base,target,'--format',a.format,'--out',delta],0 if before==after else 1)
 wire=decode(delta.read_bytes());output,copied,literal=reconstruct(before,wire)
 assert output==after
 report=run(['binary-apply',base,delta,'--out',rebuilt],0)
 assert rebuilt.read_bytes()==after and report['baseVerified'] and report['targetVerified']
 sdk=False
 if consumer:
  result=subprocess.run([str(consumer),str(base),str(target),str(delta)],capture_output=True,check=True,timeout=120)
  result=json.loads(result.stdout)
  assert all(result[key] for key in ['verified','compactVerified','streamVerified','composedVerified','sourceVerified','fileGenerationVerified','compactSinkVerified'])
  assert result['summary']['baseSha256']==wire['base']['sha256'] and result['summary']['targetSha256']==wire['target']['sha256']
  assert result['summary']['copiedBytes']==copied and result['summary']['literalBytes']==literal;sdk=True
 cases.append({'kind':kind,'base':str(base),'target':str(target),'baseBytes':len(before),'targetBytes':len(after),
               'copiedBytes':copied,'literalBytes':literal,'manifestBytes':delta.stat().st_size,'seconds':time.perf_counter()-started,'sdkVerified':sdk})
 return delta
for i,path in enumerate(inputs):
 verify(path,path,f'{i:03}-self','self')
 data=path.read_bytes();first=len(data)//3;second=len(data)*2//3
 changed=data[:first]+b'CyUEDiff byte-only mutation\x00\xff'+data[first:second]+data[min(len(data),second+19):]
 mutated=out/f'{i:03}-mutated.bin';mutated.write_bytes(changed)
 ab=verify(path,mutated,f'{i:03}-mutation','byte-edited-copy')
 if a.compose:
  final=out/f'{i:03}-final.bin';final.write_bytes(changed[127:]+changed[:127]+b'\x00\xfffinal')
  bc=verify(mutated,final,f'{i:03}-second','second-byte-edited-copy')
  verify(path,final,f'{i:03}-composed','composed',chain=[ab,bc])
 if i:verify(inputs[i-1],path,f'{i:03}-pair','different-originals')
assert all(hashlib.sha256(Path(path).read_bytes()).hexdigest()==digest for path,digest in hashes.items())
summary={'schema':'cyuediff.binary-validation/1','format':a.format,'inputs':hashes,'cases':cases,'caseCount':len(cases),
         'sdkVerifiedCases':sum(x['sdkVerified'] for x in cases),'copiedBytes':sum(x['copiedBytes'] for x in cases),
         'literalBytes':sum(x['literalBytes'] for x in cases),'allSourcesUnchanged':True}
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps({'output':str(out),'cases':len(cases),'format':a.format,'sourcesUnchanged':True}))
