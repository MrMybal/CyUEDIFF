"""Validate explicitly Skeleton-bound source poses against an independent ancestor-walk oracle."""
import argparse,hashlib,json,math,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
p.add_argument('--asset',type=Path,action='append',required=True)
p.add_argument('--skeleton',type=Path,required=True)
p.add_argument('--skeleton-object',default='')
p.add_argument('--out-dir',type=Path,default=Path('build-native/animation-pose-corpus'))
a=p.parse_args();workspace=Path(__file__).resolve().parents[1];out=a.out_dir.resolve()
if not out.is_relative_to(workspace/'build-native'):p.error('--out-dir must be under build-native')
cli=a.cli.resolve(strict=True);skeleton=a.skeleton.resolve(strict=True);assets=list(dict.fromkeys(x.resolve(strict=True) for x in a.asset))
if len(assets)>1000:p.error('At most 1,000 assets per run')
inputs={skeleton,*assets};hashes={}
for file in inputs:
 for path in [file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]]:
  if path.is_file():hashes[path]=hashlib.sha256(path.read_bytes()).hexdigest()
out.mkdir(parents=True,exist_ok=True)
def run(command,*args,codes=(0,)):
 r=subprocess.run([str(cli),command,*map(str,args)],capture_output=True,timeout=60)
 if r.returncode not in codes:raise RuntimeError((command,r.returncode,r.stderr.decode('utf-8','replace')))
 return json.loads(r.stdout)
snapshot=run('skeleton',skeleton);owners=snapshot['skeletons']
owner=a.skeleton_object or (next(iter(owners)) if len(owners)==1 else '')
if not owner or owner not in owners:p.error('Select one --skeleton-object')
bones=owners[owner]['bones']
def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def transform(point,track):
 # Rotate the scaled vector with two cross products, without constructing affine matrices.
 q=track['rotations'];norm=math.sqrt(sum(x*x for x in q));q=[x/norm for x in q]
 scaled=[point[i]*track['scales'][i] for i in range(3)]
 uv=cross(q[:3],scaled);uuv=cross(q[:3],uv)
 return [scaled[i]+2*q[3]*uv[i]+2*uuv[i]+track['positions'][i] for i in range(3)]
def oracle(name,sample,hierarchy_ready):
 if not hierarchy_ready or not sample['completeInput'] or sample['tracksCoverage']!='stored':return None
 point=[0.,0.,0.];visited=set()
 while name is not None:
  if name in visited or len(visited)>512:raise RuntimeError('Invalid oracle ancestor chain')
  visited.add(name);track=sample['tracks'].get('name:'+name)
  if not track or not track['ready']:return None
  point=transform(point,track);name=bones[name]['parent']
 return point
summary={'assets':len(assets),'models':0,'rawBones':len(bones),'sampledPoses':0,'readyJoints':0,'unavailableJoints':0,'completePoses':0,'oraclePositions':0,'selfComparisons':0,'temporalComparisons':0,'temporallyChangedJoints':0,'failures':[],'inputsUnchanged':False,'records':[]}
for index,asset in enumerate(assets):
 try:
  models=run('animation',asset)['models']
  for model_index,(model,body) in enumerate(models.items()):
   count=body.get('numberOfKeys',0)
   if not count:continue
   summary['models']+=1
   for frame in sorted({0,count//2,count-1}):
    pose=run('animation-pose',asset,skeleton,'--frame',frame,'--object',model,'--skeleton-object',owner,codes=(0,2))
    sample=run('animation-frame',asset,'--frame',frame,'--object',model,codes=(0,2))
    assert set(pose['joints'])==set(bones)
    for name,joint in pose['joints'].items():
     expected=oracle(name,sample,pose['hierarchyComplete'])
     assert joint['ready']==(expected is not None),(asset,frame,name,'availability')
     if expected is not None:
      assert all(math.isclose(x,y,rel_tol=1e-10,abs_tol=1e-7) for x,y in zip(expected,joint['position'])),(asset,frame,name,expected,joint['position'])
      summary['oraclePositions']+=1
    ready=sum(int(x['ready']) for x in pose['joints'].values())
    comparison=run('animation-pose-diff',asset,asset,'--before-skeleton',skeleton,'--after-skeleton',skeleton,'--before-frame',frame,'--after-frame',frame,'--before-object',model,'--after-object',model,'--before-skeleton-object',owner,'--after-skeleton-object',owner,codes=(0,2))
    assert comparison['changedJoints']==0 and comparison['complete']==pose['complete']
    (out/f'{index}-{model_index}-{frame}.json').write_text(json.dumps({'pose':pose,'selfComparison':comparison},indent=2),encoding='utf-8')
    summary['sampledPoses']+=1;summary['readyJoints']+=ready;summary['unavailableJoints']+=len(pose['joints'])-ready;summary['completePoses']+=int(pose['complete']);summary['selfComparisons']+=1
    summary['records'].append({'source':str(asset),'model':model,'frame':frame,'complete':pose['complete'],'readyJoints':ready,'unboundTracks':len(pose['unboundTracks'])})
   temporal=run('animation-pose-diff',asset,asset,'--before-skeleton',skeleton,'--after-skeleton',skeleton,'--before-frame',0,'--after-frame',count-1,'--before-object',model,'--after-object',model,'--before-skeleton-object',owner,'--after-skeleton-object',owner,codes=(0,1,2))
   summary['temporalComparisons']+=1;summary['temporallyChangedJoints']+=temporal['changedJoints']
   (out/f'{index}-{model_index}-temporal.json').write_text(json.dumps(temporal,indent=2),encoding='utf-8')
 except Exception as error:summary['failures'].append({'source':str(asset),'error':str(error)})
summary['inputsUnchanged']=all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in hashes.items())
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in summary.items() if k!='records'},indent=2))
if summary['failures'] or not summary['inputsUnchanged']:raise SystemExit(1)
