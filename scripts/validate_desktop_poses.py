"""Compare explicitly bound native desktop poses against the public Core CLI on read-only inputs."""
import argparse,hashlib,json,os,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
p.add_argument('--desktop',type=Path,default=Path('build-native/Release/cyuediff_desktop.exe'))
p.add_argument('--asset',type=Path,action='append',required=True)
p.add_argument('--skeleton',type=Path,required=True)
p.add_argument('--skeleton-object',default='')
p.add_argument('--locale',choices=['en','fr'],default='en')
p.add_argument('--theme',choices=['mid-grey','light-grey','amber-night','violet','navy'],default='mid-grey')
p.add_argument('--sockets',action='store_true',help='Also verify and optionally capture source socket origins')
p.add_argument('--capture',action='store_true',help='Capture the middle sampled frame of each source model')
p.add_argument('--out-dir',type=Path,default=Path('build-native/desktop-pose-corpus'))
a=p.parse_args();workspace=Path(__file__).resolve().parents[1];out=a.out_dir.resolve()
if not out.is_relative_to(workspace/'build-native'):p.error('--out-dir must stay under build-native')
assets=list(dict.fromkeys(x.resolve(strict=True) for x in a.asset));rig=a.skeleton.resolve(strict=True)
if len(assets)>1000:p.error('At most 1,000 explicit inputs per run')
cli=a.cli.resolve(strict=True);desktop=a.desktop.resolve(strict=True);hashes={}
for file in [*assets,rig]:
 for path in [file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload')]]:
  if path.is_file():hashes[path]=hashlib.sha256(path.read_bytes()).hexdigest()
if any(path.is_relative_to(out) for path in hashes):p.error('--out-dir must not contain source inputs')
out.mkdir(parents=True,exist_ok=True);env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()};env['LOCALAPPDATA']=str(out/'unused-profile')
if Path(env['LOCALAPPDATA']).exists():p.error('Expected an unused local validation profile path')
def destination(name):
 path=(out/name).resolve()
 if not path.is_relative_to(out) or path in hashes:raise RuntimeError('Output overlaps a source or escapes validation directory')
 return path
def run_cli(*args):
 r=subprocess.run([str(cli),*map(str,args)],capture_output=True,timeout=60)
 if r.returncode not in (0,2) or not r.stdout:raise RuntimeError((args,r.returncode,r.stderr.decode('utf-8','replace')))
 return json.loads(r.stdout)
summary={'schema':'cyuediff.desktop-pose-validation/1','assets':len(assets),'models':0,'sampledPoses':0,'completePoses':0,'sampledSocketPoses':0,'completeSocketPoses':0,'readySockets':0,'unavailableSockets':0,'readyJoints':0,'unavailableJoints':0,'captures':0,'failures':[],'inputsUnchanged':False,'preferencesUntouched':False,'records':[]}
for index,asset in enumerate(assets):
 try:
  models=run_cli('animation',asset)['models']
  for model_index,(model,body) in enumerate(models.items()):
   count=body.get('numberOfKeys',0)
   if not count:continue
   summary['models']+=1
   for frame in sorted({0,count//2,count-1}):
    prefix=f'{index}-{model_index}-{frame}';report=destination(prefix+'-desktop.json')
    expected=run_cli('animation-pose',asset,rig,'--frame',frame,'--object',model,'--skeleton-object',a.skeleton_object)
    args=[str(desktop),'--smoke','--no-preferences','--no-previews','--before',str(asset),'--view','animations','--pose-before-skeleton',str(rig),'--pose-before-object',model,'--pose-before-skeleton-object',a.skeleton_object,'--pose-frame',str(frame),'--locale',a.locale,'--theme',a.theme,'--report',str(report)]
    if a.sockets:args+=['--document-action','view-state:'+json.dumps({'asset':{'animationPosePreview':True,'animationSocketsPreview':True}})]
    capture=a.capture and frame==count//2
    if capture:args+=['--capture',str(destination(prefix+'.png'))]
    r=subprocess.run(args,env=env,capture_output=True,timeout=60)
    if r.returncode:raise RuntimeError((r.returncode,r.stderr.decode('utf-8','replace')))
    observed=json.loads(report.read_text(encoding='utf-8'))['document']['animationPose']
    assert observed['requested'] and observed['before']==expected,(asset,model,frame,'desktop/Core mismatch')
    if a.sockets:
     expected_sockets=run_cli('animation-socket-pose',asset,rig,'--frame',frame,'--object',model,'--skeleton-object',a.skeleton_object)
     assert observed['socketOrigins']['before']==expected_sockets,(asset,model,frame,'desktop/Core socket mismatch')
     count_ready=sum(int(j['ready']) for j in expected_sockets['sockets'].values())
     summary['sampledSocketPoses']+=1;summary['completeSocketPoses']+=int(expected_sockets['complete']);summary['readySockets']+=count_ready;summary['unavailableSockets']+=len(expected_sockets['sockets'])-count_ready
     destination(prefix+'-expected-sockets.json').write_text(json.dumps(expected_sockets,indent=2),encoding='utf-8')
    ready=sum(int(j['ready']) for j in expected['joints'].values())
    destination(prefix+'-expected.json').write_text(json.dumps(expected,indent=2),encoding='utf-8')
    summary['sampledPoses']+=1;summary['completePoses']+=int(expected['complete']);summary['readyJoints']+=ready;summary['unavailableJoints']+=len(expected['joints'])-ready;summary['captures']+=int(capture)
    summary['records'].append({'source':str(asset),'model':model,'frame':frame,'complete':expected['complete'],'readyJoints':ready})
 except Exception as error:summary['failures'].append({'source':str(asset),'error':str(error)})
summary['inputsUnchanged']=all(hashlib.sha256(path.read_bytes()).hexdigest()==digest for path,digest in hashes.items());summary['preferencesUntouched']=not Path(env['LOCALAPPDATA']).exists()
destination('summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8');print(json.dumps({k:v for k,v in summary.items() if k!='records'},indent=2))
if summary['failures'] or not summary['inputsUnchanged'] or not summary['preferencesUntouched']:raise SystemExit(1)
