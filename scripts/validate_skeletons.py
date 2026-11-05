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
profiles=Counter();coverage=Counter();classes=Counter();fingerprints={};failures=[];examples=[]
retarget_tables=retarget_sources=retarget_bound=retarget_unbound=retarget_joints=retarget_comparisons=0
animation_complete=animation_comparisons=curve_containers=curve_entries=curve_links=marker_names=marker_occurrences=fully_consumed_skeletons=0
curve_coverage=Counter();marker_coverage=Counter()
objects=bones=ready_joints=unavailable_joints=complete=reference_complete=pose_complete=self_comparisons=0
for file in files:
 for p in (file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]):
  if p.is_file():fingerprints[str(p)]=digest(p)
 run=subprocess.run([str(args.cli.resolve()),'inspect',str(file)],capture_output=True,env=env,timeout=45)
 if run.returncode:
  failures.append({'file':str(file),'error':run.stderr.decode('utf-8','replace')[:500]});continue
 data=json.loads(run.stdout);profiles[str(data['summary']['ue5Version'])]+=1
 if any(body['class']=='/Script/Engine.Skeleton' for body in data['skeletons']['skeletons'].values()):
  metadata=subprocess.run([str(args.cli.resolve()),'skeleton-animation',str(file)],capture_output=True,env=env,timeout=45)
  if metadata.returncode:
   failures.append({'file':str(file),'animationError':metadata.stderr.decode('utf-8','replace')[:500]})
  else:
   animation=json.loads(metadata.stdout);animation_complete+=animation['complete']
   compared=subprocess.run([str(args.cli.resolve()),'skeleton-animation-diff',str(file),str(file)],capture_output=True,env=env,timeout=45)
   if compared.returncode!=(0 if animation['complete'] else 2) or json.loads(compared.stdout).get('changes')!=[]:
    failures.append({'file':str(file),'animationComparisonError':compared.stderr.decode('utf-8','replace')[:500]})
   else:animation_comparisons+=1

 for name,body in data['skeletons']['skeletons'].items():
  classes[body['class']]+=1;reference_complete+=body['referenceComplete']
  if body['class']=='/Script/Engine.Skeleton':
   curves=body['curveMetadata'];markers=body['syncMarkers']
   curve_coverage[curves['coverage']]+=1;marker_coverage[markers['coverage']]+=1
   curve_containers+=len(curves['containers'])
   for container in curves['containers'].values():
    curve_entries+=len(container)
    curve_links+=sum(len(c['linkedBones']) for c in container.values())
   marker_names+=len(markers['names']);marker_occurrences+=sum(m['occurrences'] for m in markers['names'].values())
   fully_consumed_skeletons+=not any(r.get('bytes',0) for r in body['native']['opaque'].values() if isinstance(r,dict))

  objects+=1;bones+=len(body['bones']);complete+=bool(body['complete']);coverage[body['native']['coverage']]+=1
  pose=subprocess.run([str(args.cli.resolve()),'skeleton-pose',str(file),'--object',name],capture_output=True,env=env,timeout=45)
  if pose.returncode:
   failures.append({'file':str(file),'poseError':pose.stderr.decode('utf-8','replace')[:500]});continue
  pose_data=json.loads(pose.stdout);pose_complete+=bool(pose_data['complete']);joints=pose_data['joints']
  comparison=subprocess.run([str(args.cli.resolve()),'skeleton-pose-diff',str(file),str(file),'--before-object',name,'--after-object',name],capture_output=True,env=env,timeout=45)
  expected=0 if pose_data['complete'] else 2
  if comparison.returncode!=expected or json.loads(comparison.stdout).get('changedJoints')!=0:
   failures.append({'file':str(file),'poseComparisonError':comparison.stderr.decode('utf-8','replace')[:500]})
  else:self_comparisons+=1
  ready_joints+=sum(bool(j['ready']) for j in joints.values())
  unavailable_joints+=sum(not j['ready'] for j in joints.values())
  table=body['retargeting']
  retarget_tables+=table['coverage']=='stored'
  for key,source in table['sources'].items():
   retarget_sources+=1;retarget_bound+=bool(source['complete']);retarget_unbound+=not source['complete']
   selected=subprocess.run([str(args.cli.resolve()),'skeleton-pose',str(file),'--object',name,'--retarget-source',key],capture_output=True,env=env,timeout=45)
   if selected.returncode:
    failures.append({'file':str(file),'retargetError':selected.stderr.decode('utf-8','replace')[:500]});continue
   source_pose=json.loads(selected.stdout)
   retarget_joints+=sum(j['ready'] for j in source_pose['joints'].values())
   compared=subprocess.run([str(args.cli.resolve()),'skeleton-pose-diff',str(file),str(file),'--before-object',name,'--after-object',name,'--before-retarget-source',key],capture_output=True,env=env,timeout=45)
   if compared.returncode!=(0 if source_pose['complete'] else 2):
    failures.append({'file':str(file),'retargetComparisonError':compared.stderr.decode('utf-8','replace')[:500]})
   else:retarget_comparisons+=1
  if len(examples)<10:examples.append({'file':str(file),'object':name,'bones':len(body['bones']),'coverage':body['native']['coverage']})
assert all(digest(Path(p))==value for p,value in fingerprints.items()),'Source or companion bytes changed'
report={'packages':len(files),'fileProfiles':dict(profiles),'skeletonObjects':objects,'storedBones':bones,
        'animationCompletePackages':animation_complete,'animationSelfComparisons':animation_comparisons,
        'curveCoverage':dict(curve_coverage),'markerCoverage':dict(marker_coverage),'curveContainers':curve_containers,
        'curveEntries':curve_entries,'curveBoneLinks':curve_links,'syncMarkerNames':marker_names,
        'syncMarkerOccurrences':marker_occurrences,'fullyConsumedSkeletonNativeBodies':fully_consumed_skeletons,
        'retargetStoredTables':retarget_tables,'retargetSources':retarget_sources,'retargetBoundSources':retarget_bound,'retargetUnboundSources':retarget_unbound,'retargetReadyJoints':retarget_joints,'retargetSelfComparisons':retarget_comparisons,
        'completeStoredProjections':complete,'completeRawReferences':reference_complete,'completeReferencePoses':pose_complete,'objectClasses':dict(classes),'verifiedPoseSelfComparisons':self_comparisons,'nativeCoverage':dict(coverage),'readyReferenceJoints':ready_joints,
        'unavailableReferenceJoints':unavailable_joints,'failures':failures,'examples':examples,'sourceFingerprints':fingerprints}
(output/'summary.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('examples','sourceFingerprints')},indent=2))
print('Original source/companion hashes unchanged; pose space is raw-reference-affine.')
if failures:raise SystemExit(1)
