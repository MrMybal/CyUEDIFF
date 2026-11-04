"""Validate texture inspection, previews and diffs on workspace copies of a real asset.

Use a monolithic Texture2D with one JPEG source in an uncompressed v2 package
trailer (for example T_caustic). This exercises the reader; it is not an Unreal
asset writer. The controlled JPEG mutation does not update its Unreal content ID.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset',type=Path,required=True)
parser.add_argument('--exe',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/texture-validation'))
args=parser.parse_args()
original=args.asset.resolve(strict=True)
exe=args.exe.resolve(strict=True)
out=args.output_dir.resolve()
out.mkdir(parents=True,exist_ok=True)
protected=[original,*[original.with_suffix(ext) for ext in ('.uexp','.ubulk','.upayload','.uptnl','.m.ubulk','.o.ubulk')]]

def target(name):
    path=out/name
    for source in protected:
        if path.resolve()==source or (path.exists() and source.exists() and path.samefile(source)):
            raise RuntimeError('Validation output aliases an input asset or companion')
    return path

def run(*arguments,expected=0):
    result=subprocess.run([str(exe),*map(str,arguments)],capture_output=True,text=True,encoding='utf-8')
    if result.returncode!=expected:
        raise RuntimeError(f'Unexpected exit {result.returncode}: {result.stderr}')
    return json.loads(result.stdout)

raw=original.read_bytes()
checksum=hashlib.sha256(raw).hexdigest()
report=run('inspect',original)
textures=report['textures']['textures']
assert len(textures)==1 and report['completeInput']
name,texture=next(iter(textures.items()))
assert texture['compression']=='TSCF_JPEG' and texture['payload']['bufferMethod']==0
assert texture['layers']==texture['slices']==texture['sourceMips']==1
assert report['fingerprints']['package']['sha256']==checksum
preview=run('preview',original,'--out',target('before.png'))
assert (preview['width'],preview['height'])==(texture['width'],texture['height'])
identity=run('image-diff',original,original,'--heatmap',target('identical-mask.png'))
assert identity['changedPixels']==0 and identity['status']=='identical'

# Read the indexed name table, then locate one complete bool tag inside its export.
names=[]
pos=report['summary']['nameOffset']
for _ in range(report['summary']['nameCount']):
    count=struct.unpack_from('<i',raw,pos)[0];pos+=4
    length=abs(count)*(2 if count<0 else 1)
    names.append(raw[pos:pos+length].decode('utf-16-le' if count<0 else 'utf-8').rstrip('\0'))
    pos+=length+4
coverage=report['coverage'][name]
start=coverage['offset'];end=start+coverage['nativeStart']
assert end<=len(raw)
prop='bNormalizeNormals'
assert texture['settings'][prop]['value'] is True
pattern=struct.pack('<iiiiii',names.index(prop),0,names.index('BoolProperty'),0,0,0)
positions=[];position=raw.find(pattern,start,end)
while position>=0:
    positions.append(position);position=raw.find(pattern,position+1,end)
assert len(positions)==1
changed=bytearray(raw);position=positions[0]+24
bit=0x10 if report['summary']['ue5Version']>=1012 else 1
assert changed[position]&bit
changed[position]&=~bit
settings_copy=target('settings.uasset');settings_copy.write_bytes(changed)
settings_diff=run('diff',original,settings_copy,expected=1)
assert any(item['path'].endswith('/settings/bNormalizeNormals/value') for item in settings_diff['textureChanges'])
settings_pixels=run('image-diff',original,settings_copy)
assert settings_pixels['changedPixels']==0

# Resolve the JPEG via the validated v2 trailer entry, not a signature scan.
magic,length,tag=struct.unpack_from('<QQI',raw,len(raw)-20)
assert magic==0x29bfca045138de76 and tag==0x9e2a83c1
trailer=len(raw)-length
head,version,header,payload=struct.unpack_from('<QIIQ',raw,trailer)
assert head==0xd1c43b2e80a5f697 and version==2
count=struct.unpack_from('<i',raw,trailer+24)[0]
entry=None
for i in range(count):
    at=trailer+28+i*49
    identity_bytes,offset,stored,size,flags,filter_flags,access=struct.unpack_from('<20sqQQHHB',raw,at)
    if identity_bytes.hex()==texture['payload']['id']:
        assert access==0 and flags==0
        entry=(offset,stored,size)
assert entry is not None
offset,stored,size=entry
buffer=trailer+header+offset
assert raw[buffer+8]==0 and stored==size+64
assert hashlib.sha256(raw[buffer:buffer+stored]).hexdigest()==texture['payload']['sha256']
source_start=buffer+64
jpeg=raw[source_start:source_start+size]
assert jpeg[:2]==b'\xff\xd8'
pos=2;quant=None
while pos<len(jpeg):
    assert jpeg[pos]==255
    marker=jpeg[pos+1];pos+=2
    if marker in (0xda,0xd9):break
    segment=struct.unpack_from('>H',jpeg,pos)[0]
    assert segment>=2 and pos+segment<=len(jpeg)
    if marker==0xdb:
        assert jpeg[pos+2]>>4==0
        quant=pos+3;break
    pos+=segment
assert quant is not None
changed=bytearray(raw);at=source_start+quant
changed[at]=changed[at]+1 if changed[at]<254 else changed[at]-1
pixel_copy=target('pixels.uasset');pixel_copy.write_bytes(changed)
pixel_diff=run('diff',original,pixel_copy,expected=1)
assert any(item['path'].endswith('/payload/sha256') for item in pixel_diff['textureChanges'])
pixels=run('image-diff',original,pixel_copy,'--heatmap',target('pixel-mask.png'),expected=1)
assert pixels['changedPixels']>0 and pixels['maxChannelDelta']>0
run('preview',pixel_copy,'--out',target('after.png'))

# All rejected CLI operations must preserve the source and any existing outputs.
for command in [
    ['preview',original,'--out',original],
    ['image-diff',original,original,'--heatmap',original],
    ['image-diff',original,original,'--threshold','256'],
    ['image-diff',original,original,'--threshold','-1'],
    ['preview',original,'--out',target('invalid.png'),'--mip','32'],
    ['preview',original,'--out',target('invalid.png'),'--object','missing'],
    ['image-diff',original,original,'--out',target('collision.png'),'--heatmap',target('collision.png')],
]:
    result=subprocess.run([str(exe),*map(str,command)],capture_output=True)
    assert result.returncode==2 and not result.stdout
assert not target('invalid.png').exists() and not target('collision.png').exists()
assert hashlib.sha256(original.read_bytes()).hexdigest()==checksum
result={'asset':str(original),'originalSha256':checksum,'texture':texture,'preview':preview,
        'settingsDiff':settings_diff,'settingsPixels':settings_pixels,'pixelDiff':pixel_diff,
        'pixels':pixels,'originalUnchanged':True,'rejectedCliCases':7}
target('report.json').write_text(json.dumps(result,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
print(json.dumps({'texture':name,'width':texture['width'],'height':texture['height'],
                  'changedPixels':pixels['changedPixels'],'originalUnchanged':True,'report':str(target('report.json'))},indent=2))
