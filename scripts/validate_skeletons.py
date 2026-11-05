"""Inspect explicit local skeleton/package corpora without redistributing engine assets."""
import argparse,hashlib,json,os,subprocess
from collections import Counter
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--content',type=Path,action='append',default=[])
parser.add_argument('--asset',type=Path,action='append',default=[])
parser.add_argument('--max-files',type=int,default=1000)
parser.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/skeleton-corpus'))
args=parser.parse_args()
workspace=Path(__file__).resolve().parents[1];output=args.output_dir.resolve()
if not output.is_relative_to(workspace) or not output.relative_to(workspace).parts or not output.relative_to(workspace).parts[0].startswith('build'):
 parser.error('Reports require an ignored workspace build directory')
if not args.content and not args.asset:parser.error('Select --content or --asset explicitly')
roots=[p.resolve(strict=True) for p in args.content];assets=[p.resolve(strict=True) for p in args.asset]
if args.max_files<1 or any(output==r or output.is_relative_to(r) or r.is_relative_to(output) for r in roots+assets):
 parser.error('Invalid file limit or overlapping corpus/output roots')
files=sorted(set(assets)|{p.resolve() for root in roots for p in root.rglob('*') if p.suffix in ('.uasset','.umap') and p.is_file()})[:args.max_files]
output.mkdir(parents=True,exist_ok=True)
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
def digest(path):
 h=hashlib.sha256()
 with path.open('rb') as stream:
  for block in iter(lambda:stream.read(1024*1024),b''):h.update(block)
 return h.hexdigest()
profiles=Counter();coverage=Counter();fingerprints={};failures=[];examples=[]
objects=bones=ready_joints=unavailable_joints=complete=0
for file in files:
 for p in (file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]):
  if p.is_file():fingerprints[str(p)]=digest(p)
 run=subprocess.run([str(args.cli.resolve()),'inspect',str(file)],capture_output=True,env=env,timeout=45)
 if run.returncode:
  failures.append({'file':str(file),'error':run.stderr.decode('utf-8','replace')[:500]});continue
 data=json.loads(run.stdout);profiles[str(data['summary']['ue5Version'])]+=1
 for name,body in data['skeletons']['skeletons'].items():
  objects+=1;bones+=len(body['bones']);complete+=bool(body['complete']);coverage[body['native']['coverage']]+=1
  pose=subprocess.run([str(args.cli.resolve()),'skeleton-pose',str(file),'--object',name],capture_output=True,env=env,timeout=45)
  if pose.returncode:
   failures.append({'file':str(file),'poseError':pose.stderr.decode('utf-8','replace')[:500]});continue
  joints=json.loads(pose.stdout)['joints']
  ready_joints+=sum(bool(j['ready']) for j in joints.values())
  unavailable_joints+=sum(not j['ready'] for j in joints.values())
  if len(examples)<10:examples.append({'file':str(file),'object':name,'bones':len(body['bones']),'coverage':body['native']['coverage']})
assert all(digest(Path(p))==value for p,value in fingerprints.items()),'Source or companion bytes changed'
report={'packages':len(files),'fileProfiles':dict(profiles),'skeletonObjects':objects,'storedBones':bones,
        'completeStoredProjections':complete,'nativeCoverage':dict(coverage),'readyReferenceJoints':ready_joints,
        'unavailableReferenceJoints':unavailable_joints,'failures':failures,'examples':examples,'sourceFingerprints':fingerprints}
(output/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('examples','sourceFingerprints')},indent=2))
print('Original source/companion hashes unchanged; pose space is raw-reference-affine.')
if failures:raise SystemExit(1)
