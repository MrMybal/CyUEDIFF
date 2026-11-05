"""Independent byte reconstruction and failure/no-overwrite checks for the public binary CLI."""
from pathlib import Path
import copy,hashlib,json,os,random,subprocess,sys,tempfile
exe=str(Path(sys.argv[1]).resolve());checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-binary-cli-',dir=Path.cwd()) as tmp:
 root=Path(tmp);a=root/'avant-é.uasset';b=root/'after.uasset';delta=root/'delta.json';out=root/'reconstructed.uasset'
 base=random.Random(17).randbytes(8195);target=base[:129]+bytes(range(255))+base[129:4098]+base[4173:]
 a.write_bytes(base);b.write_bytes(target)
 def run(*args,code=0):
  global checks
  p=subprocess.run([exe,*map(str,args)],capture_output=True,timeout=45)
  assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(p.stdout) if p.stdout else None
 run('binary-diff',a,b,'--block-size',64,'--out',delta,code=1)
 manifest=json.loads(delta.read_text(encoding='utf-8'))
 # Independent interpreter uses only the portable wire contract and Python SHA-256.
 rebuilt=bytearray()
 for op in manifest['operations']:
  if op['kind']=='copy':rebuilt+=base[op['offset']:op['offset']+op['bytes']]
  else:rebuilt+=bytes.fromhex(op['hex'])
 assert rebuilt==target and hashlib.sha256(rebuilt).hexdigest()==manifest['target']['sha256']
 result=run('binary-apply',a,delta,'--out',out)
 assert result['baseVerified'] and result['targetVerified'] and out.read_bytes()==target
 run('binary-apply',a,delta,'--out',out,code=2);assert out.read_bytes()==target
 stats=run('binary-diff',a,b,'--block-size',64,'--summary',code=1)
 assert stats['copiedBytes']>7500 and stats['literalBytes']<500
 assert run('binary-diff',a,b,'--block-size',64,code=1)==manifest
 run('binary-diff',a,a,'--summary')
 run('binary-diff',a,b,'--out',a,code=2)
 run('binary-diff',a,b,'--out',a.with_suffix('.ubulk'),code=2)
 assert not a.with_suffix('.ubulk').exists()
 alias=root/'alias.json';os.link(a,alias)
 run('binary-diff',a,b,'--out',alias,code=2);assert alias.read_bytes()==base
 bad=root/'bad.json';failed=root/'must-not-exist.bin'
 variants=[]
 v=copy.deepcopy(manifest);v['base']['sha256']='0'*64;variants.append(json.dumps(v))
 v=copy.deepcopy(manifest);v['target']['sha256']='0'*64;variants.append(json.dumps(v))
 v=copy.deepcopy(manifest);v['operations'][0]={'kind':'copy','offset':len(base),'bytes':100};variants.append(json.dumps(v))
 v=copy.deepcopy(manifest);v['operations'][0]={'kind':'literal','hex':'zz'};variants.append(json.dumps(v))
 variants+=['{','{"schema":"future"}', '{"schema":"future",'+json.dumps(manifest)[1:]]
 for text in variants:
  bad.write_text(text,encoding='utf-8');run('binary-apply',a,bad,'--out',failed,code=2);assert not failed.exists()
 wrong=root/'wrong.bin';wrong.write_bytes(base[:-1]+bytes([base[-1]^1]))
 run('binary-apply',wrong,delta,'--out',failed,code=2);assert not failed.exists()
 for args in [('binary-apply',a,delta),('binary-apply',a,delta,'--summary'),('binary-diff',a,b,'--block-size',0),
              ('binary-diff',a,b,'--max-input-bytes',1),('binary-diff',a,b,'--max-literal-bytes',0),
              ('binary-diff',a,b,'--max-manifest-bytes',1),('binary-diff',a,b,'--block-size','1e6'),
              ('binary-diff',a,b,'--block-size','18446744073709551616'),('binary-diff',a,b,'--codec','unused'),
              ('binary-diff',a,b,'--out',failed,'--out',failed),('binary-diff',a,b,'--out',root/'missing/out.json')]:
  run(*args,code=2);assert not failed.exists()
 empty=root/'empty.bin';empty.write_bytes(b'');empty_delta=root/'empty.json';empty_out=root/'empty-rebuilt.bin'
 run('binary-diff',a,empty,'--out',empty_delta,code=1);run('binary-apply',a,empty_delta,'--out',empty_out)
 assert empty_out.read_bytes()==b''
 assert a.read_bytes()==base and b.read_bytes()==target
 assert not list(root.glob('*.cyuediff-tmp-*'))
print(f'{checks} binary CLI process checks passed')
