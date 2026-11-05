"""Verify historical desktop poses against an installed Git SDK consumer and caller-supplied original files."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
for name in ('repository','probe','before-asset','after-asset','before-rig','after-rig'):
 p.add_argument('--'+name,type=Path,required=True)
for name in ('before-revision','after-revision','before','after','before-skeleton','after-skeleton'):
 p.add_argument('--'+name,required=True)
p.add_argument('--before-project',default='');p.add_argument('--after-project',default='')
p.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
p.add_argument('--desktop',type=Path,default=Path('build-native/Release/cyuediff_desktop.exe'))
p.add_argument('--out-dir',type=Path,required=True)
p.add_argument('--locale',choices=['en','fr'],default='en')
p.add_argument('--theme',choices=['mid-grey','light-grey','amber-night','violet','navy'],default='mid-grey')
p.add_argument('--capture',action='store_true')
a=p.parse_args();workspace=Path(__file__).resolve().parents[1];out=a.out_dir.resolve();repo=a.repository.resolve(strict=True)
if not out.is_relative_to(workspace/'build-native') or out.is_relative_to(repo) or repo.is_relative_to(out):p.error('Output must stay under build-native and separate from the input repository')
if bool(a.before_project)!=bool(a.after_project):p.error('Supply both historical project descriptors or neither')
files=[x.resolve(strict=True) for x in (a.before_asset,a.after_asset,a.before_rig,a.after_rig)];hashes={}
for file in files:
 for path in [file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]]:
  if path.is_file():hashes[path]=hashlib.sha256(path.read_bytes()).hexdigest()
if any(path.is_relative_to(out) for path in hashes):p.error('Output contains an original input')
cli=a.cli.resolve(strict=True);desktop=a.desktop.resolve(strict=True);probe=a.probe.resolve(strict=True)
out.mkdir(parents=True,exist_ok=True);env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};env['LOCALAPPDATA']=str(out/'unused-profile')
if Path(env['LOCALAPPDATA']).exists():p.error('Validation profile must be unused')
def destination(name):
 path=(out/name).resolve()
 if not path.is_relative_to(out) or path in hashes:raise RuntimeError('Output escapes validation directory or overlaps input')
 return path
def execute(executable,args):
 result=subprocess.run([str(executable),*map(str,args)],env=env,capture_output=True,timeout=90)
 if result.returncode not in (0,1,2) or not result.stdout:raise RuntimeError((args,result.returncode,result.stderr.decode('utf-8','replace')))
 return json.loads(result.stdout)
counts=[]
for file in files[:2]:
 models=execute(cli,['animation',file])['models']
 if len(models)!=1:p.error('Validation requires exactly one source animation model in each supplied file')
 count=next(iter(models.values())).get('numberOfKeys',0)
 if not count:p.error('Both source models require a known nonzero key count')
 counts.append(count)
summary={'schema':'cyuediff.git-desktop-pose-validation/1','sampledComparisons':0,'sampledPoses':0,'readyJoints':0,'completePoses':0,'records':[]}
for index,frames in enumerate(zip((0,counts[0]//2,counts[0]-1),(0,counts[1]//2,counts[1]-1))):
 args=[repo,a.before_revision,a.after_revision,a.before,a.after,a.before_skeleton,a.after_skeleton,*frames]
 if a.before_project:args += [a.before_project,a.after_project]
 sdk=execute(probe,args)
 expected=[execute(cli,['animation-pose',files[side],files[side+2],'--frame',frames[side]]) for side in (0,1)]
 assert sdk['before']==expected[0] and sdk['after']==expected[1],'Installed SDK/original Core pose mismatch'
 report=destination(f'{index}-desktop.json')
 args=['--smoke','--no-preferences','--no-previews','--repo',repo,'--before-revision',a.before_revision,'--after-revision',a.after_revision,'--before',a.before,'--after',a.after,'--pose-before-skeleton',a.before_skeleton,'--pose-after-skeleton',a.after_skeleton,'--pose-before-frame',frames[0],'--pose-after-frame',frames[1],'--view','animations','--locale',a.locale,'--theme',a.theme,'--report',report]
 if a.before_project:args += ['--before-project',a.before_project,'--after-project',a.after_project,'--no-engine']
 if a.capture and index==1:args += ['--capture',destination('middle.png')]
 result=subprocess.run([str(desktop),*map(str,args)],env=env,capture_output=True,timeout=90)
 if result.returncode:raise RuntimeError(result.stderr.decode('utf-8','replace'))
 observed=json.loads(report.read_text(encoding='utf-8'))['document']['animationPose']
 assert observed['before']==sdk['before'] and observed['after']==sdk['after'] and observed['comparison']==sdk['comparison'],'Desktop/installed SDK pose mismatch'
 for side,label in enumerate(('before','after')):
  revision=sdk[label+'Binding'];assert observed[label+'Binding']['revision']==revision,'Skeleton provenance mismatch'
  assert revision['commit']==sdk[label+'Revision']['commit'],'Animation/Skeleton commit mismatch'
  summary['sampledPoses']+=1;summary['completePoses']+=int(expected[side]['complete']);summary['readyJoints']+=sum(int(j['ready']) for j in expected[side]['joints'].values())
 for role,file,revision in [('beforeAnimation',files[0],sdk['beforeRevision']),('afterAnimation',files[1],sdk['afterRevision']),('beforeSkeleton',files[2],sdk['beforeBinding']),('afterSkeleton',files[3],sdk['afterBinding'])]:
  stem=Path(revision['path']).stem
  for name,digest in sdk['fingerprints'][role].items():
   assert name.startswith(stem+'.'),'Unexpected materialized companion name'
   reference=file.with_suffix(name[len(stem):])
   assert reference in hashes and digest==hashes[reference],'Historical file differs from supplied original: '+str(reference)
 destination(f'{index}-sdk.json').write_text(json.dumps(sdk,indent=2),encoding='utf-8')
 summary['sampledComparisons']+=1;summary['records'].append({'frames':frames,'status':sdk['comparison']['status'],'beforeCommit':sdk['beforeRevision']['commit'],'afterCommit':sdk['afterRevision']['commit']})
summary['inputsUnchanged']=all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in hashes.items());summary['preferencesUntouched']=not Path(env['LOCALAPPDATA']).exists()
assert summary['inputsUnchanged'] and summary['preferencesUntouched']
destination('summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps(summary,indent=2))
