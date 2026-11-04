"""Validate a modern static mesh reader on workspace copies, without opening Unreal.

The controlled geometry copy replaces a single local v2 trailer payload with an
uncompressed buffer and changes one source coordinate. Unreal's content ID is
not recomputed: this is a reader test, not a production Unreal asset writer.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import zlib

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset',type=Path,required=True)
parser.add_argument('--codec',type=Path,required=True)
parser.add_argument('--exe',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/mesh-validation'))
args=parser.parse_args()
original=args.asset.resolve(strict=True);exe=args.exe.resolve(strict=True);codec=args.codec.resolve(strict=True)
out=args.output_dir.resolve();out.mkdir(parents=True,exist_ok=True)
protected=[original,codec,*[original.with_suffix(ext) for ext in ('.uexp','.ubulk','.upayload','.uptnl','.m.ubulk','.o.ubulk')]]

def target(name):
    file=out/name
    for source in protected:
        if file.resolve()==source or (file.exists() and source.exists() and file.samefile(source)):
            raise RuntimeError('Output aliases a protected input')
    return file

def run(*arguments,expected=0,with_codec=False):
    command=[str(exe),*map(str,arguments)]
    if with_codec:command+=['--codec',str(codec)]
    result=subprocess.run(command,capture_output=True,text=True,encoding='utf-8')
    if result.returncode!=expected:raise RuntimeError(f'Unexpected exit {result.returncode}: {result.stderr}')
    return json.loads(result.stdout)

raw=original.read_bytes();checksum=hashlib.sha256(raw).hexdigest();inspection=run('inspect',original)
assert inspection['completeInput'] and inspection['fingerprints']['package']['sha256']==checksum
# --out writes JSON to disk, so read it separately rather than parsing stdout.
subprocess.run([str(exe),'geometry',str(original),'--codec',str(codec),'--out',str(target('before.json'))],check=True)
before=json.loads(target('before.json').read_text(encoding='utf-8'))
assert before['vertices'] and before['instances'] and before['triangles']
identity=run('mesh-diff',original,original,with_codec=True)
assert identity['status']=='identical' and identity['changes']==[]
export=run('export-mesh',original,'--out',target('mesh.obj'),with_codec=True)
lines=target('mesh.obj').read_text(encoding='utf-8').splitlines()
assert sum(line.startswith('f ') for line in lines)==len(before['triangles'])
assert sum(line.startswith('v ') for line in lines)==len(before['vertices'])

# Resolve one source payload through its indexed content ID.
sources=[s for s in inspection['meshes']['sources'].values() if s['payload']['rawBytes']]
assert len(sources)==1
source=sources[0];magic,length,tag=struct.unpack_from('<QQI',raw,len(raw)-20)
assert magic==0x29bfca045138de76 and tag==0x9e2a83c1
trailer=len(raw)-length
head,version,header,payload_bytes,count=struct.unpack_from('<QIIQi',raw,trailer)
assert head==0xd1c43b2e80a5f697 and version==2 and count==1 and header==77
identity_bytes,offset,stored,size,flags,filters,access=struct.unpack_from('<20sqQQHHB',raw,trailer+28)
assert identity_bytes.hex()==source['payload']['id'] and offset==0 and access==0 and not flags
encoded=raw[trailer+header:trailer+header+stored]
assert hashlib.sha256(encoded).hexdigest()==source['payload']['sha256']
method,exponent=encoded[8],encoded[11];blocks=struct.unpack_from('>I',encoded,12)[0]
if method==0:payload=bytearray(encoded[64:])
else:
    assert method==3
    dll=ctypes.CDLL(str(codec));decode=dll.cyuediff_decode_block_v1
    decode.argtypes=[ctypes.c_uint32,ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t];decode.restype=ctypes.c_size_t
    sizes=struct.unpack_from('>'+str(blocks)+'I',encoded,64);position=64+blocks*4;payload=bytearray()
    for block_size in sizes:
        chunk=encoded[position:position+block_size];position+=block_size;raw_size=min(1<<exponent,size-len(payload))
        if block_size==raw_size:payload.extend(chunk)
        else:
            input_buffer=ctypes.create_string_buffer(chunk);output=ctypes.create_string_buffer(raw_size)
            assert decode(method,input_buffer,block_size,output,raw_size)==raw_size
            payload.extend(output.raw)
assert len(payload)==size

# Read the first element container and its Position attribute through the format.
pos=4

def integer():
    global pos
    result=struct.unpack_from('<i',payload,pos)[0];pos+=4;return result

def string():
    global pos
    count=integer();assert count>0
    result=payload[pos:pos+count-1].decode('utf-8');pos+=count;return result

assert string()=='Vertices' and integer()==1
bits=integer();word_count=(bits+31)//32;words=struct.unpack_from('<'+'I'*word_count,payload,pos);pos+=word_count*4
holes=integer();elements=integer();attributes=integer()
assert attributes==1 and string() in ('Position','Position ')
assert integer()==1 and integer()==1 and integer()==elements and integer()==1
assert integer()==1 and integer()==12 and integer()==elements
vertex=next(i for i in range(bits) if words[i//32]&(1<<(i%32)))
coordinate=pos+vertex*12
old=struct.unpack_from('<f',payload,coordinate)[0];new=old+1.0
struct.pack_into('<f',payload,coordinate,new)

# All lengths and header CRC are updated; stored Unreal content identities stay unchanged.
new_header=bytearray(struct.pack('>IIBBBBIQQ',0xb7756362,0,0,0,0,0,1,len(payload),len(payload)+64)+encoded[32:64])
struct.pack_into('>I',new_header,4,zlib.crc32(new_header[8:])&0xffffffff)
new_buffer=new_header+payload
new_trailer=struct.pack('<QIIQi',head,2,77,len(new_buffer),1)
new_trailer+=struct.pack('<20sqQQHHB',identity_bytes,0,len(new_buffer),size,0,filters,0)
new_trailer+=new_buffer
new_trailer+=struct.pack('<QQI',magic,len(new_trailer)+20,tag)
copy=target('position.uasset');copy.write_bytes(raw[:trailer]+new_trailer)
position_diff=run('mesh-diff',original,copy,with_codec=True,expected=1)
path=f'/vertices/{vertex}/position/0'
assert any(change['path']==path and change['before']==old and change['after']==new for change in position_diff['changes'])
assert not any(change['path'].startswith(('/instances/','/triangles/','/materialSlots/')) for change in position_diff['changes'])
package_diff=run('diff',original,copy,expected=1)
assert any(change['path'].endswith('/payload/sha256') for change in package_diff['meshChanges'])

# Change a build/lighting setting on another copy, preserving source geometry.
names=[];pos=inspection['summary']['nameOffset']
for _ in range(inspection['summary']['nameCount']):
    count=struct.unpack_from('<i',raw,pos)[0];pos+=4;length=abs(count)*(2 if count<0 else 1)
    names.append(raw[pos:pos+length].decode('utf-16-le' if count<0 else 'utf-8').rstrip('\0'));pos+=length+4
mesh_name=next(iter(inspection['meshes']['meshes']))
coverage=inspection['coverage'][mesh_name]
pattern=struct.pack('<iiiiii',names.index('LightMapResolution'),0,names.index('IntProperty'),0,4,0)
field=raw.index(pattern,coverage['offset'],coverage['offset']+coverage['bytes'])+len(pattern)+1
assert inspection['summary']['ue5Version']<1011
changed=bytearray(raw);setting=struct.unpack_from('<i',raw,field)[0];struct.pack_into('<i',changed,field,setting+64)
settings_copy=target('settings.uasset');settings_copy.write_bytes(changed)
settings_diff=run('diff',original,settings_copy,expected=1)
settings_geometry=run('mesh-diff',original,settings_copy,with_codec=True)
assert settings_geometry['status']=='identical'
assert any(change['path'].endswith('/settings/LightMapResolution/value') for change in settings_diff['meshChanges'])

bad_commands=[
    ['geometry',original],
    ['geometry',original,'--codec',codec,'--lod','99'],
    ['geometry',original,'--codec',codec,'--object','missing'],
    ['geometry',original,'--codec',codec,'--metadata'],
    ['export-mesh',original,'--codec',codec,'--out',original],
    ['geometry',original,'--codec',codec,'--out',codec],
]
if method==0:bad_commands=bad_commands[1:]
for command in bad_commands:
    result=subprocess.run([str(exe),*map(str,command)],capture_output=True)
    assert result.returncode==2 and not result.stdout,(command,result.stdout,result.stderr)
assert hashlib.sha256(original.read_bytes()).hexdigest()==checksum
result={'asset':str(original),'originalSha256':checksum,'vertices':len(before['vertices']),
        'instances':len(before['instances']),'triangles':len(before['triangles']),
        'materialGroups':before['materialSlots'],'positionDiff':position_diff,
        'settingsDiff':settings_diff,'settingsGeometry':settings_geometry,'export':export,
        'originalUnchanged':True,'contentIdRecomputedForCopy':False,'rejectedCliCases':len(bad_commands)}
target('report.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({key:result[key] for key in ('vertices','instances','triangles','originalUnchanged','rejectedCliCases')},indent=2))
