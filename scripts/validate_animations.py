"""Validate stored animation models and source frames without modifying the input corpus."""
import argparse,hashlib,json,subprocess
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--asset',type=Path,action='append',default=[])
parser.add_argument('--content',type=Path)
parser.add_argument('--max-files',type=int,default=1000)
parser.add_argument('--out-dir',type=Path,default=Path('build-native/animation-corpus'))
args=parser.parse_args()
if not 1<=args.max_files<=100000:parser.error('--max-files must be 1..100000')
workspace=Path(__file__).resolve().parents[1];out=args.out_dir.resolve()
if not out.is_relative_to(workspace/'build-native'):parser.error('--out-dir must be under build-native')
cli=args.cli.resolve(strict=True);out.mkdir(parents=True,exist_ok=True)
files=list(args.asset)
if args.content:files+=sorted([*args.content.rglob('*.uasset'),*args.content.rglob('*.umap')])[:args.max_files]
files=list(dict.fromkeys(p.resolve(strict=True) for p in files))[:args.max_files]
if not files:parser.error('Supply --asset or --content with loose assets')
hashes={}
for file in files:
 for p in [file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.upayload')]]:
  if p.is_file():hashes[p]=hashlib.sha256(p.read_bytes()).hexdigest()
def run(command,file,destination,*extra,code=0):
 result=subprocess.run([str(cli),command,str(file),*map(str,extra),'--out',str(destination)],capture_output=True,timeout=60)
 if result.returncode not in (code if isinstance(code,tuple) else (code,)):raise RuntimeError(f'{command}: {file.name}: {result.returncode}: {result.stderr.decode("utf-8","replace")}')
 return json.loads(destination.read_text(encoding='utf-8'))
summary={'files':len(files),'models':0,'tracks':0,'storedKeys':0,'sampledFrames':0,'readyTracks':0,
         'completeModels':0,'frameSelfComparisons':0,'completeFrameSelfComparisons':0,'comparedLocalTracks':0,'unavailableLocalTracks':0,'temporalComparisons':0,'temporallyChangedTracks':0,'temporallyUnavailableTracks':0,'failures':[],'inputsUnchanged':False,'assets':[]}
for index,file in enumerate(files):
 try:
  root=run('animation',file,out/f'{index}-models.json')
  record={'source':str(file),'models':[]}
  for model_index,(path,model) in enumerate(root['models'].items()):
   summary['models']+=1;summary['tracks']+=len(model['tracks'])
   summary['completeModels']+=int(model['complete'])
   summary['storedKeys']+=sum(len(t.get(channel,[])) for t in model['tracks'].values() for channel in ('positions','rotations','scales'))
   run('animation-diff',file,out/f'{index}-{model_index}-self.json',file,'--before-object',path,'--after-object',path,
       code=0 if model['complete'] and root['completeInput'] else 2)
   n=model.get('numberOfKeys',0);sampled=[]
   if n:
    for frame in sorted({0,n//2,n-1}):
     report=run('animation-frame',file,out/f'{index}-{model_index}-frame-{frame}.json','--object',path,'--frame',frame)
     summary['sampledFrames']+=1
     ready=sum(int(t['ready']) for t in report['tracks'].values());summary['readyTracks']+=ready
     assert all(len(t[c])==(4 if c=='rotations' else 3) for t in report['tracks'].values() for c in ('positions','rotations','scales') if c in t)
     rate=model.get('frameRate',{})
     if 'numerator' in rate and 'denominator' in rate:assert report['timeSeconds']==frame*rate['denominator']/rate['numerator']
     else:assert 'timeSeconds' not in report
     comparison=run('animation-frame-diff',file,out/f'{index}-{model_index}-frame-{frame}-self.json',file,'--before-object',path,'--after-object',path,'--before-frame',frame,'--after-frame',frame,code=0 if report['complete'] else 2)
     assert comparison['changedTracks']==0
     summary['frameSelfComparisons']+=1;summary['completeFrameSelfComparisons']+=int(comparison['complete']);summary['comparedLocalTracks']+=comparison['comparedTracks'];summary['unavailableLocalTracks']+=comparison['unavailableTracks']
     sampled.append({'frame':frame,'complete':report['complete'],'readyTracks':ready,'localSelfStatus':comparison['status']})
    temporal=run('animation-frame-diff',file,out/f'{index}-{model_index}-temporal.json',file,'--before-object',path,'--after-object',path,'--before-frame',0,'--after-frame',n-1,code=(0,1,2))
    summary['temporalComparisons']+=1;summary['temporallyChangedTracks']+=temporal['changedTracks'];summary['temporallyUnavailableTracks']+=temporal['unavailableTracks']

   record['models'].append({'path':path,'complete':model['complete'],'tracks':len(model['tracks']),'frames':sampled})
  summary['assets'].append(record)
 except Exception as error:summary['failures'].append({'source':str(file),'error':str(error)})
summary['inputsUnchanged']=all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items())
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='assets'},indent=2))
if summary['failures'] or not summary['inputsUnchanged']:raise SystemExit(1)
