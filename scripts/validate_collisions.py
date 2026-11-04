"""Read an explicitly selected local collision corpus; retain reports only in ignored build folders."""
import argparse,hashlib,json,os,subprocess
from collections import Counter
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--content',type=Path,action='append',required=True)
parser.add_argument('--wireframe',action='store_true',help='Also validate geometry previews for packages with bodies')
parser.add_argument('--max-files',type=int,default=1000)
parser.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/collision-corpus-077'))
args=parser.parse_args()
workspace=Path(__file__).resolve().parents[1]
output=args.output_dir.resolve()
if not output.is_relative_to(workspace) or not output.relative_to(workspace).parts[0].startswith('build'):
 parser.error('Reports require an ignored workspace build directory')
roots=[p.resolve(strict=True) for p in args.content]
if args.max_files<1 or any(output==r or output.is_relative_to(r) or r.is_relative_to(output) for r in roots):
 parser.error('Invalid file limit or overlapping corpus/output roots')
output.mkdir(parents=True,exist_ok=True)
files=sorted({p.resolve() for r in roots for p in r.rglob('*') if p.suffix in ('.uasset','.umap') and p.is_file()})[:args.max_files]
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
fingerprints={};kinds=Counter();missing=Counter();failures=[];examples=[]
body_count=packages_with_bodies=complete=incomplete=unresolved_owners=0
preview_ready=preview_unavailable=preview_edges=0
preview_diagnostics=Counter()
for file in files:
 for path in (file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]):
  if path.is_file():fingerprints[str(path)]=digest(path)
 r=subprocess.run([str(args.cli.resolve()),'collision',str(file)],capture_output=True,env=env,timeout=45)
 if r.returncode:
  failures.append({'file':str(file),'error':r.stderr.decode('utf-8','replace')[:500]});continue
 data=json.loads(r.stdout)
 if data['bodies']:
  packages_with_bodies+=1
  if data['complete']:complete+=1
  else:incomplete+=1
  if len(examples)<10:examples.append({'file':str(file),'bodies':list(data['bodies'])})
 if args.wireframe and data['bodies']:
  preview_result=subprocess.run([str(args.cli.resolve()),'collision-preview',str(file)],capture_output=True,env=env,timeout=45)
  if preview_result.returncode:
   failures.append({'file':str(file),'previewError':preview_result.stderr.decode('utf-8','replace')[:500]})
  else:
   preview=json.loads(preview_result.stdout)
   for shape in preview['shapes']:
    preview_ready+=shape['ready']
    preview_unavailable+=not shape['ready']
    preview_edges+=len(shape['lines'])
    preview_diagnostics.update(shape['diagnostics'])
 body_count+=len(data['bodies'])
 for body in data['bodies'].values():
  for shape in body['shapes'].values():
   kinds[shape['kind']]+=1
   for key in shape['unresolvedProperties']:missing[key]+=1
   for message in shape['diagnostics']:
    if message.startswith('Missing or unresolved'):missing[message]+=1
 unresolved_owners+=sum(not owner['complete'] for owner in data['owners'].values())
assert all(digest(Path(p))==h for p,h in fingerprints.items()),'Source or companion bytes changed'
report={'packages':len(files),'packagesWithBodies':packages_with_bodies,'bodies':body_count,
        'completeStoredProjections':complete,'incompleteStoredProjections':incomplete,
        'shapeKinds':dict(kinds),'unresolvedGeometry':dict(missing),'unresolvedOwners':unresolved_owners,
        'wireframe':{'requested':args.wireframe,'readyShapes':preview_ready,'unavailableShapes':preview_unavailable,'edges':preview_edges,'diagnostics':dict(preview_diagnostics)},
        'failures':failures,'examples':examples,'sourceFingerprints':fingerprints}
(output/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('sourceFingerprints','examples')},indent=2))
print('Original source/companion hashes unchanged; completeness covers only the stored projection.')

if failures:raise SystemExit(1)
