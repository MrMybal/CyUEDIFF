"""Validate virtualized editor payloads using read-only originals and workspace copies.

Extracts existing local v2 trailer buffers unchanged, converts only the copied
trailer index to virtualized references, and compares decoded pixels/geometry.
These controlled reader fixtures are not an Unreal asset writer or an
independently produced Unreal virtualized corpus.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--texture',type=Path,required=True)
p.add_argument('--mesh',type=Path,required=True)
p.add_argument('--codec',type=Path,required=True)
p.add_argument('--exe',type=Path,required=True)
p.add_argument('--desktop',type=Path)
p.add_argument('--output-dir',type=Path,required=True)
a=p.parse_args()
exe=a.exe.resolve(strict=True); codec=a.codec.resolve(strict=True)
originals=[a.texture.resolve(strict=True),a.mesh.resolve(strict=True)]
root=a.output_dir.resolve(); workspace=Path.cwd().resolve()
if workspace not in root.parents:
    raise RuntimeError('Validation output must be inside the current workspace')
root.mkdir(parents=True,exist_ok=True)
if any(root.iterdir()):
    raise RuntimeError("Choose a fresh empty validation output directory")
protected={f:hashlib.sha256(f.read_bytes()).hexdigest() for original in originals for f in
           [original,*[original.with_suffix(ext) for ext in ('.uexp','.ubulk','.uptnl','.upayload','.m.ubulk','.o.ubulk')]] if f.is_file()}
if any(root==f or root in f.parents for f in protected):
    raise RuntimeError('Validation output contains an original input')
checks=0

def run(*args,code=0):
    global checks
    result=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=60)
    if result.returncode!=code:
        raise RuntimeError((result.returncode,args,result.stdout,result.stderr))
    checks+=1
    return json.loads(result.stdout or result.stderr)

def extract(original,label):
    raw=original.read_bytes()
    magic,length,tag=struct.unpack_from('<QQI',raw,len(raw)-20)
    assert magic==0x29bfca045138de76 and tag==0x9e2a83c1
    at=len(raw)-length
    head,version,header,payload,count=struct.unpack_from('<QIIQi',raw,at)
    assert head==0xd1c43b2e80a5f697 and version==2 and header==28+49*count
    assert payload+header+20==length
    table=bytearray(raw[at:at+header]); struct.pack_into('<Q',table,16,0)
    store=root/f'{label}-store'; store.mkdir()
    files=[]
    for i in range(count):
        e=28+i*49
        identity,offset,stored,size,flags,filters,access=struct.unpack_from('<20sqQQHHB',table,e)
        assert access==0 and not flags and 0<=offset<=payload and stored<=payload-offset
        encoded=raw[at+header+offset:at+header+offset+stored]
        assert encoded[:4]==bytes.fromhex('b7756362') and encoded[32:52]==identity
        assert int.from_bytes(encoded[16:24],'big')==size and int.from_bytes(encoded[24:32],'big')==stored
        name=identity.hex()
        path=store/name[:2]/name[2:4]/name[4:6]/(name[6:]+'.upayload')
        path.parent.mkdir(parents=True,exist_ok=True); path.write_bytes(encoded); files.append(path)
        struct.pack_into('<qQ',table,e+20,-1,0); table[e+48]=2
    copy=root/f'{label}-virtualized.uasset'
    copy.write_bytes(raw[:at]+table+struct.pack('<QQI',magic,header+20,tag))
    inspection=run('inspect',copy)
    return copy,store,files,inspection

texture,texture_store,texture_files,tmeta=extract(originals[0],'texture')
mesh,mesh_store,mesh_files,mmeta=extract(originals[1],'mesh')
assert all(v['payload']['storage']=='virtualized' for v in tmeta['textures']['textures'].values())
assert all(v['payload']['storage']=='virtualized' for v in mmeta['meshes']['sources'].values() if v['payload']['rawBytes'])
assert run('preview',originals[0],'--out',root/'original.png')['width']>0
assert run('preview',texture,'--payload-root',texture_store,'--out',root/'virtualized.png')['width']>0
assert (root/'original.png').read_bytes()==(root/'virtualized.png').read_bytes()
assert run('image-diff',originals[0],texture,'--after-payload-root',texture_store)['changedPixels']==0
assert run('image-diff',texture,texture,'--payload-root',texture_store)['changedPixels']==0
local=run('geometry',originals[1],'--codec',codec)
remote=run('geometry',mesh,'--codec',codec,'--payload-root',mesh_store)
assert local==remote
assert run('mesh-diff',originals[1],mesh,'--codec',codec,'--after-payload-root',mesh_store)['status']=='identical'
run('export-mesh',mesh,'--codec',codec,'--payload-root',mesh_store,'--out',root/'virtualized.obj')
for command,asset,store in [('preview',texture,texture_store),('export-mesh',mesh,mesh_store)]:
    assert run(command,asset,'--codec',codec,'--out',root/'missing.out',code=2)['error']=='virtualized-root'
    run(command,asset,'--codec',codec,'--payload-root',store,'--out',store/'must-not-write.out',code=2)
    assert not (store/'must-not-write.out').exists()
# The second side uses its explicit store and performs a full content check.
bad_store=root/'corrupt-store'
for source in texture_files:
    path=bad_store/source.relative_to(texture_store);path.parent.mkdir(parents=True,exist_ok=True)
    corrupt=bytearray(source.read_bytes());corrupt[-1]^=1;path.write_bytes(corrupt)
assert run('image-diff',texture,texture,'--payload-root',texture_store,'--after-payload-root',bad_store,code=2)['error']=='bulk-hash'
run('inspect',texture,'--payload-root',texture_store,code=2)

if a.desktop:
    desktop=a.desktop.resolve(strict=True)
    env={k.upper():v for k,v in os.environ.items()};env['LOCALAPPDATA']=str(root/'unused-profile')
    def ui(*args):
        global checks
        report=root/'desktop-report.json'; report.unlink(missing_ok=True)
        result=subprocess.run([str(desktop),'--smoke','--no-preferences','--codec',str(codec),'--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=90)
        if result.returncode!=0: raise RuntimeError((result.returncode,result.stderr))
        checks+=1
        return json.loads(report.read_text(encoding='utf-8'))
    saved=root/'payload-workspace.json'
    doc=ui('--before',texture,'--after',texture,'--payload-root',texture_store,'--after-payload-root',texture_store,'--save-workspace',saved)
    assert len(doc['document']['previews'])==2
    restored=ui('--workspace',saved,'--locale','fr')
    assert restored['document']==doc['document']
    settings=json.loads(saved.read_text(encoding='utf-8'))['documents'][0]['settings']
    assert settings['payload_root']==str(texture_store) and settings['after_payload_root']==str(texture_store)
    assert not settings['inherit_after_payload_root']
    meshdoc=ui('--before',mesh,'--after',mesh,'--payload-root',mesh_store,'--view','meshes')
    assert meshdoc['document']['beforeMesh'] and meshdoc['document']['afterMesh']
for path,digest in protected.items():
    assert hashlib.sha256(path.read_bytes()).hexdigest()==digest
report={'schema':'cyuediff.virtualized-validation/1','checks':checks,'originalsUnchanged':len(protected),
        'textureBuffers':len(texture_files),'meshBuffers':len(mesh_files),
        'texturePngSha256':hashlib.sha256((root/'virtualized.png').read_bytes()).hexdigest(),
        'meshVertices':len(remote['vertices']),'meshTriangles':len(remote['triangles']),
        'scope':'Controlled virtualized reader fixtures extracted from real local editor assets; not an independently produced virtualized Unreal build.'}
(root/'validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
