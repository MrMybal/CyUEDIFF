"""Inspect explicit, host-owned loose-package corpora without redistributing assets."""
import argparse
from collections import Counter
import hashlib
import json
import os
from pathlib import Path
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--content',type=Path,action='append',required=True)
parser.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/corpus-validation'))
parser.add_argument('--max-files',type=int,default=1000)
args=parser.parse_args()
workspace=Path(__file__).resolve().parents[1]
out=args.output_dir.resolve()
try: relative=out.relative_to(workspace)
except ValueError: parser.error('Reports must stay in an ignored workspace build directory')
if not relative.parts or not relative.parts[0].startswith('build') or args.max_files<1:parser.error('Invalid report directory or file limit')
roots=[p.resolve(strict=True) for p in args.content]
if any(out==p or out.is_relative_to(p) or p.is_relative_to(out) for p in roots):parser.error('Outputs overlap corpus roots')
out.mkdir(parents=True,exist_ok=True)
exe=args.cli.resolve(strict=True)
files=sorted({p.resolve() for root in roots for p in root.rglob('*') if p.suffix in ('.uasset','.umap') and p.is_file()})[:args.max_files]
counts=Counter();types=Counter();opaque=Counter();failures=[];fingerprints={}
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as f:
  for chunk in iter(lambda:f.read(1024*1024),b''):h.update(chunk)
 return h.hexdigest()
def visit(value):
 if isinstance(value,dict):
  tag=value.get('type',{})
  if isinstance(tag,dict) and 'name' in tag:types[tag['name']]+=1
  unknown=value.get('opaque')
  if isinstance(unknown,dict):opaque[unknown.get('reason','unclassified')]+=1
  for child in value.values():visit(child)
 elif isinstance(value,list):
  for child in value:visit(child)
for file in files:
 companions=[file,*[file.with_suffix(e) for e in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]]
 for p in companions:
  if p.is_file():fingerprints[str(p)]=digest(p)
 result=subprocess.run([str(exe),'inspect',str(file)],capture_output=True,timeout=45)
 if result.returncode:
  try:error=json.loads(result.stderr)
  except ValueError:error={'error':'process','message':result.stderr.decode('utf-8','replace')[:200]}
  failures.append({'file':str(file),'error':error});continue
 data=json.loads(result.stdout)
 for entry in data['coverage'].values():counts[entry['coverage']]+=1
 visit(data['snapshot']['objects'])
for name,value in fingerprints.items():
 if digest(Path(name))!=value:raise RuntimeError('Original corpus changed: '+name)
corrupt=0
for i,file in enumerate(files[:3]):
 with file.open('rb') as source:prefix=source.read(128)
 for length in (0,4,32,128):
  sample=out/f'truncated-{i}-{length}.uasset';sample.write_bytes(prefix[:length])
  result=subprocess.run([str(exe),'inspect',str(sample)],capture_output=True,timeout=10)
  if result.returncode!=2:raise RuntimeError('Truncated header was accepted')
  corrupt+=1
report={'packages':len(files),'exports':dict(counts),'propertyTypes':dict(types),'opaque':dict(opaque),
        'failures':failures,'corruptHeadersRejected':corrupt,'sourceFingerprints':fingerprints}
(out/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('sourceFingerprints','failures')},indent=2))
print(f'{len(failures)} package failures; original source and companion hashes unchanged')
