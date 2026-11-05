"""Compact wire interoperability, composition and transactional streamed CLI publication."""
from pathlib import Path
import hashlib,json,random,struct,subprocess,sys,tempfile
exe=str(Path(sys.argv[1]).resolve());checks=0
def decode(raw):
 if not raw.startswith(b'CYUEDLT\0'):return json.loads(raw)
 magic,version,flags,block,reserved,base,target,count,a,b=struct.unpack_from('<8sIIIIQQQ32s32s',raw)
 assert version==1 and flags==reserved==0
 at=112;ops=[]
 for _ in range(count):
  tag=raw[at];at+=1
  if tag==0:
   offset,n=struct.unpack_from('<QQ',raw,at);at+=16;ops.append({'kind':'copy','offset':offset,'bytes':n})
  else:
   assert tag==1;n,=struct.unpack_from('<Q',raw,at);at+=8
   assert n>0 and n<=len(raw)-at;ops.append({'kind':'literal','hex':raw[at:at+n].hex()});at+=n
 assert at==len(raw)
 return {'schema':'cyuediff.binary-delta/1','blockSize':block,'base':{'bytes':base,'sha256':a.hex()},
         'target':{'bytes':target,'sha256':b.hex()},'operations':ops}
def rebuild(raw,base):
 d=decode(raw);out=b''.join(base[x['offset']:x['offset']+x['bytes']] if x['kind']=='copy' else bytes.fromhex(x['hex']) for x in d['operations'])
 assert len(base)==d['base']['bytes'] and hashlib.sha256(base).hexdigest()==d['base']['sha256']
 assert len(out)==d['target']['bytes'] and hashlib.sha256(out).hexdigest()==d['target']['sha256']
 return out
with tempfile.TemporaryDirectory(prefix='.test-tmp-binary-workflow-',dir=Path.cwd()) as tmp:
 root=Path(tmp);a=root/'a.bin';b=root/'b.bin';c=root/'c.bin'
 av=random.Random(166).randbytes(8192);bv=av[:123]+b'\0\xffnew'+av[123:4096]+av[4103:]
 cv=bv[1000:]+bv[:1000]+b'\xff\x80final'
 for path,data in [(a,av),(b,bv),(c,cv)]:path.write_bytes(data)
 def run(*args,code=0):
  global checks
  r=subprocess.run([exe,*map(str,args)],capture_output=True,timeout=45)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(r.stdout) if r.stdout else None
 manifests={}
 for name,base,target,wanted in [('ab',a,b,bv),('bc',b,c,cv)]:
  for fmt in ['json','binary']:
   file=root/(name+'-'+fmt+'.dat');manifests[name,fmt]=file
   run('binary-diff',base,target,'--block-size',64,'--format',fmt,'--out',file,code=1)
   assert rebuild(file.read_bytes(),base.read_bytes())==wanted
   report=run('binary-info',file);assert report['schema']=='cyuediff.binary-info/1' and not report['identitiesVerified']
 for left in ['json','binary']:
  for right in ['json','binary']:
   for fmt in ['json','binary']:
    file=root/(left+'-'+right+'-'+fmt+'.delta');out=file.with_suffix('.rebuilt')
    run('binary-compose',manifests['ab',left],manifests['bc',right],'--format',fmt,'--out',file)
    assert rebuild(file.read_bytes(),av)==cv
    result=run('binary-apply',a,file,'--out',out);assert out.read_bytes()==cv and result['targetVerified']
 plain=run('binary-compose',manifests['ab','binary'],manifests['bc','json'])
 assert rebuild(json.dumps(plain).encode(),av)==cv
 summary=run('binary-compose',manifests['ab','json'],manifests['bc','binary'],'--summary')
 assert summary['targetSha256']==hashlib.sha256(cv).hexdigest()
 protected={p:p.read_bytes() for p in root.iterdir() if p.is_file()}
 failed=root/'failed.bin'
 bad=root/'corrupt.delta';raw=bytearray(manifests['ab','binary'].read_bytes());raw[80]^=1;bad.write_bytes(raw)
 run('binary-apply',a,bad,'--out',failed,code=2);assert not failed.exists() and not list(root.glob('*.cyuediff-tmp-*'))
 assert run('binary-diff',a,b,'--max-source-read-bytes',1048576,'--summary',code=1)['targetSha256']==hashlib.sha256(bv).hexdigest()
 for args in [
  ('binary-diff',a,b,'--max-source-read-bytes',1,'--out',failed),
  ('binary-diff',a,b,'--max-source-read-bytes',0,'--out',failed),
  ('binary-diff',a,b,'--max-source-read-bytes','-1','--out',failed),
  ('binary-diff',a,b,'--max-source-read-bytes',1,'--max-source-read-bytes',2,'--out',failed),
  ('binary-apply',a,manifests['ab','binary'],'--max-source-read-bytes',1048576,'--out',failed),
  ('binary-info',manifests['ab','binary'],'--max-source-read-bytes',1048576),
  ('binary-compose',manifests['ab','json'],manifests['bc','binary'],'--max-source-read-bytes',1048576,'--out',failed),
  ('binary-diff',a,b,'--format','binary'),
  ('binary-diff',a,b,'--format','binary','--summary','--out',failed),
  ('binary-diff',a,b,'--format','zip','--out',failed),
  ('binary-apply',a,manifests['ab','binary'],'--format','binary','--out',failed),
  ('binary-info',manifests['ab','binary'],'--summary'),
  ('binary-info',manifests['ab','binary'],'--format','json'),
  ('binary-info',manifests['ab','binary'],'--block-size',64),
  ('binary-info',manifests['ab','binary'],'--out',manifests['ab','binary']),
  ('binary-compose',manifests['ab','json'],manifests['ab','binary'],'--out',failed),
  ('binary-compose',manifests['ab','json'],manifests['bc','binary'],'--out',manifests['ab','json']),
  ('binary-compose',manifests['ab','json'],manifests['bc','binary'],'--max-composition-fragments',1,'--out',failed),
  ('binary-compose',manifests['ab','json'],manifests['bc','binary'],'--max-composition-fragments',0,'--out',failed),
  ('binary-diff',a,b,'--max-composition-fragments',20,'--out',failed),
  ('binary-diff',a,b,'--format','binary','--max-manifest-bytes',112,'--out',failed),
  ('binary-compose',manifests['ab','json'],manifests['bc','binary'],'--block-size',64,'--out',failed)
 ]:
  run(*args,code=2);assert not failed.exists()
 assert all(p.read_bytes()==data for p,data in protected.items())
 # Composition needs only manifests; no endpoint is read or silently recreated.
 for path in [a,b,c]:path.unlink()
 result=run('binary-compose',manifests['ab','binary'],manifests['bc','binary'])
 assert rebuild(json.dumps(result).encode(),av)==cv and not any(p.exists() for p in [a,b,c])
 assert not list(root.glob('*.cyuediff-tmp-*'))
print(f'{checks} compact/composition/streaming CLI checks passed')
