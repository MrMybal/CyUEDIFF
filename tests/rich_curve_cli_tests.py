"""Stored curve projection, named comparison, evaluation and protected CLI outputs."""
from pathlib import Path
import tempfile,subprocess,json,sys,hashlib
cli,generator=(str(Path(p).resolve()) for p in sys.argv[1:3]);checks=0
with tempfile.TemporaryDirectory(prefix='rich-curve-cli-') as tmp:
 root=Path(tmp);subprocess.run([generator,'--write-fixtures',str(root)],check=True)
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.uasset')}
 def run(*args,code=0):
  global checks
  r=subprocess.run([cli,*map(str,args)],capture_output=True,timeout=30)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'))
  checks+=1
  return json.loads(r.stdout) if r.stdout else None
 for version in (0,1004,1013):
  files=[root/f'curve-{n}-{version}.uasset' for n in range(9)]
  before,after,inserted,duplicate,weighted,empty,unknown,unordered,cubic=files
  assert run('curves',before)['complete']
  assert run('curves-diff',before,before)['status']=='identical'
  assert len(run('curves-diff',before,after,code=1)['changes'])==1
  assert len(run('curves-diff',before,inserted,code=1)['changes'])==2
  assert run('curves-diff',duplicate,duplicate,code=2)['status']=='incomplete'
  assert run('curve-value',before,'--time','.5')['value']==5.
  assert run('curve-value',cubic,'--time','.5')['value']==4.625
  assert run('curve-value',weighted,'--time','.5')['ready']
  assert run('curve-value',weighted,'--time','0')['ready']
  for asset in (empty,unknown,unordered,duplicate):assert not run('curve-value',asset,'--time','.5',code=2)['ready']
  for args in [('curve-value',before),('curve-value',before,'--time','nan'),('curve-value',before,'--time','inf'),
               ('curve-value',before,'--time','-1'),('curve-value',before,'--time','.5','--lane','missing'),
               ('curves',before,'--time','1'),('curves',before,'--metadata'),('curves',before,'--snapshot'),
               ('curves-diff',before,after,'--before-object','missing')]:
   run(*args,code=2)
  grid=['--time-start','0','--time-end','2','--samples','5']
  assert run('curve-value-diff',before,before,*grid)['status']=='identical'
  diff=run('curve-value-diff',before,after,*grid,code=1)
  assert diff['changedSamples']==3 and diff['maximumAbsoluteDelta']==2.
  assert run('curve-value-diff',before,after,*grid,'--value-tolerance','2')['status']=='identical'
  assert run('curve-value-diff',before,before,'--time-start','-1','--time-end','2','--samples','5',code=2)['status']=='incomplete'
  for args in [('curve-value-diff',before,after),('curve-value-diff',before,after,*grid,'--value-tolerance','-1'),
               ('curve-value-diff',before,after,'--time-start','0','--time-end','2','--samples','0'),
               ('curve-value-diff',before,after,'--time-start','0','--time-end','2','--samples','4097'),
               ('curve-value-diff',before,after,*grid,'--before-lane','missing'),
               ('curves',before,'--time-start','0')]:
   run(*args,code=2)
  run('curve-value-diff',before,after,*grid,'--out',before,code=2)
  for target in (before,before.with_suffix('.uexp')):run('curves',before,'--out',target,code=2)
  out=root/'report.json';run('curve-value',before,'--time','.5','--out',out)
  assert json.loads(out.read_text(encoding='utf-8'))['ready']
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items())
help_text=subprocess.check_output([cli,'--help'],text=True)
assert '\\n' not in help_text and any(line.startswith('  cyuediff curve-value ') for line in help_text.splitlines())
print(f'{checks} rich curve CLI checks passed')
