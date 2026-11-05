"""Read-only stored rich-curve corpus validation with explicit coverage and evaluator gaps."""
from pathlib import Path
import argparse,hashlib,json,subprocess
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--curve-family',choices=('float','transform'),default='float')
parser.add_argument('--cli',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--asset',type=Path,action='append',default=[])
parser.add_argument('--content',type=Path)
parser.add_argument('--max-files',type=int,default=1000)
parser.add_argument('--out-dir',type=Path,default=Path('build-native/curve-corpus'))
args=parser.parse_args()
if not 1<=args.max_files<=100000:parser.error('--max-files must be 1..100000')
workspace=Path(__file__).resolve().parents[1];out=args.out_dir.resolve()
if not out.is_relative_to(workspace/'build-native'):parser.error('--out-dir must stay under build-native')
cli=args.cli.resolve(strict=True);out.mkdir(parents=True,exist_ok=True)
files=list(args.asset)
if args.content:files+=sorted([*args.content.rglob('*.uasset'),*args.content.rglob('*.umap')])[:args.max_files]
files=list(dict.fromkeys(p.resolve(strict=True) for p in files))[:args.max_files]
if not files:parser.error('Supply --asset or --content')
hashes={}
for file in files:
 for p in [file,*[file.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.upayload')]]:
  if p.is_file():hashes[p]=hashlib.sha256(p.read_bytes()).hexdigest()
def run(command,file,destination,*extra,codes=(0,)):
 r=subprocess.run([str(cli),command,str(file),*map(str,extra),'--curve-family',args.curve_family,'--out',str(destination)],capture_output=True,timeout=60)
 if r.returncode not in codes:raise RuntimeError(f'{command}: {file.name}: {r.returncode}: {r.stderr.decode("utf-8","replace")}')
 return json.loads(destination.read_text(encoding='utf-8')),r.returncode
summary={'curveFamily':args.curve_family,'componentGroups':0,'readyComponentGroups':0,'componentValues':0,'readyComponentValues':0,'files':len(files),'owners':0,'lanes':0,'keys':0,'completeOwners':0,'samples':0,'readySamples':0,'sampledComparisons':0,'completeSampledComparisons':0,'sampledValues':0,'comparedValues':0,'unavailableValues':0,'failures':[],'inputsUnchanged':False,'assets':[]}
for index,file in enumerate(files):
 try:
  root,_=run('curves',file,out/f'{index}-curves.json')
  record={'source':str(file),'owners':[]}
  for owner_index,(path,owner) in enumerate(root['owners'].items()):
   summary['owners']+=1;summary['lanes']+=len(owner['lanes']);summary['completeOwners']+=int(owner['complete'])
   run('curves-diff',file,out/f'{index}-{owner_index}-self.json',file,'--before-object',path,'--after-object',path,
       codes=(0 if owner['complete'] and root['completeInput'] else 2,))
   entry={'path':path,'complete':owner['complete'],'lanes':[]}
   for lane_index,(lane_id,lane) in enumerate(owner['lanes'].items()):
    summary['keys']+=len(lane['keys']);times=[lane['keys'][key]['time'] for key in lane['keyOrder'] if 'time' in lane['keys'][key]]
    samples=[]
    coordinates={-.5,0.,.5}
    if times:
     width=max(max(times)-min(times),1.)
     coordinates={min(times),max(times),(min(times)+max(times))/2,min(times)-width/2,max(times)+width/2}
     if len(times)>1:coordinates.add((times[0]+times[1])/2)
    for sample_index,t in enumerate(sorted(coordinates)):
     report,code=run('curve-value',file,out/f'{index}-{owner_index}-{lane_index}-sample-{sample_index}.json',
        '--time',repr(t),'--object',path,'--lane',lane_id,codes=(0,2))
     assert code==(0 if report['ready'] else 2)
     summary['samples']+=1;summary['readySamples']+=int(report['ready'])
     samples.append({'time':t,'ready':report['ready'],'method':report['method'],'value':report['value']})
    grid,code=run('curve-value-diff',file,out/f'{index}-{owner_index}-{lane_index}-sampled-self.json',file,
       '--time-start',repr(min(coordinates)),'--time-end',repr(max(coordinates)),'--samples','5',
       '--before-object',path,'--after-object',path,'--before-lane',lane_id,'--after-lane',lane_id,codes=(0,2))
    assert grid['changedSamples']==0 and grid['maximumAbsoluteDelta'] in (None,0.)
    assert code==(0 if grid['complete'] else 2)
    summary['sampledComparisons']+=1;summary['completeSampledComparisons']+=int(grid['complete'])
    summary['sampledValues']+=grid['sampleCount'];summary['comparedValues']+=grid['comparedSamples'];summary['unavailableValues']+=grid['unavailableSamples']
    entry['lanes'].append({'id':lane_id,'complete':lane['complete'],'keys':len(lane['keys']),'samples':samples,'sampledSelfComparison':{'status':grid['status'],'sampleCount':grid['sampleCount'],'comparedSamples':grid['comparedSamples'],'unavailableSamples':grid['unavailableSamples']}})
   animation=owner['class']=='/Script/Engine.AnimDataModel'
   groups=list(owner.get('curveGroups',{})) if animation and args.curve_family=='transform' else list(owner['lanes']) if animation else ['']
   entry['componentGroups']=[]
   for group_index,group in enumerate(groups):
    selected=[lane for key,lane in owner['lanes'].items() if not animation or (lane.get('curveId',key)==group)]
    times=[key['time'] for lane in selected for key in lane['keys'].values() if 'time' in key]
    coordinate=(min(times)+max(times))/2 if times else .5
    selectors=['--curve-id',group] if group else []
    value,code=run('curve-components',file,out/f'{index}-{owner_index}-group-{group_index}.json','--time',repr(coordinate),'--object',path,*selectors,codes=(0,2))
    assert code==(0 if value['ready'] else 2)
    summary['componentGroups']+=1;summary['readyComponentGroups']+=int(value['ready']);summary['componentValues']+=len(value['components']);summary['readyComponentValues']+=sum(int(v['ready']) for v in value['components'].values())
    entry['componentGroups'].append({'id':group,'time':coordinate,'ready':value['ready'],'values':value['values']})
   record['owners'].append(entry)
  summary['assets'].append(record)
 except Exception as error:summary['failures'].append({'source':str(file),'error':str(error)})
summary['inputsUnchanged']=all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k!='assets'},indent=2))
if summary['failures'] or not summary['inputsUnchanged']:raise SystemExit(1)
