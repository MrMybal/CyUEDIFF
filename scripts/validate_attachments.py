"""Validate explicit local skeletal attachment corpora without redistributing input assets."""
import argparse,hashlib,json,os,subprocess
from collections import Counter
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset',type=Path,action='append',default=[])
parser.add_argument('--content',type=Path,action='append',default=[])
parser.add_argument('--max-files',type=int,default=1000)
parser.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/attachment-corpus'))
args=parser.parse_args()
workspace=Path(__file__).resolve().parents[1];output=args.output_dir.resolve()
if not output.is_relative_to(workspace) or not output.relative_to(workspace).parts or not output.relative_to(workspace).parts[0].startswith('build'):
 parser.error('Reports require an ignored workspace build directory')
if not args.asset and not args.content:parser.error('Select --asset or --content explicitly')
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
def run(*arguments):
 p=subprocess.run([str(args.cli.resolve()),*map(str,arguments)],capture_output=True,env=env,timeout=45)
 if p.returncode not in (0,1,2) or not p.stdout:raise RuntimeError(p.stderr.decode('utf-8','replace')[:500])
 return p.returncode,json.loads(p.stdout)
counts=Counter();lists=Counter();virtual_coverage=Counter();fingerprints={};failures=[];examples=[]
for file in files:
 for source in (file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]):
  if source.is_file():fingerprints[str(source)]=digest(source)
 try:
  code,data=run('attachments',file)
  if code:raise RuntimeError('Input payload is incomplete')
  counts['owners']+=len(data['owners']);counts['sockets']+=len(data['sockets'])
  counts['storedLocalLocations']+=sum('location' in s for s in data['sockets'].values())
  counts['completeMetadataPackages']+=bool(data['complete'])
  if data['owners']:
   code,comparison=run('attachments-diff',file,file)
   if code!=(0 if data['complete'] else 2) or comparison['changes']:raise RuntimeError('Attachment self-comparison status mismatch')
   counts['metadataSelfComparisons']+=1
  for owner,body in data['owners'].items():
   lists[body['socketListCoverage']]+=1
   virtual=body['virtualBones'];virtual_coverage[virtual['coverage']]+=1
   counts['socketReferences']+=len(body['socketReferences'])
   counts['unresolvedReferences']+=sum(not r['resolved'] and r['resolution']!='null' for r in body['socketReferences'])
   counts['virtualDefinitions']+=len(virtual['definitions'])
   counts['cycleBlockedDefinitions']+=sum(v['cycleBlocked'] for v in virtual['definitions'].values())
   code,pose=run('socket-pose',file,'--object',owner)
   if code:raise RuntimeError('Socket origin inspection failed')
   counts['completeSocketPoses']+=bool(pose['complete']);counts['socketPoints']+=len(pose['sockets'])
   counts['readySocketOrigins']+=sum(s['ready'] for s in pose['sockets'].values())
   code,compared=run('socket-pose-diff',file,file,'--before-object',owner,'--after-object',owner)
   if code!=(0 if pose['complete'] else 2) or compared['changedSockets']!=0:raise RuntimeError('Socket origin self-comparison status mismatch')
   counts['poseSelfComparisons']+=1
   if len(examples)<12:examples.append({'file':str(file),'owner':owner,'sockets':len(pose['sockets']),'complete':pose['complete']})
 except (RuntimeError,ValueError,KeyError) as error:failures.append({'file':str(file),'error':str(error)})
assert all(digest(Path(path))==value for path,value in fingerprints.items()),'Original input/companion bytes changed'
report={'packages':len(files),**dict(counts),'socketListCoverage':dict(lists),'virtualCoverage':dict(virtual_coverage),'failures':failures,'examples':examples,'sourceFingerprints':fingerprints}
(output/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('examples','sourceFingerprints')},indent=2))
print('Original source/companion hashes unchanged; no defaults or external objects loaded.')
if failures:raise SystemExit(1)
