"""Validate explicit host-owned UE5.3 texture mips with a separate wire reader and Pillow.

Requires Pillow for independent BC/raw pixel decoding. Reports and raw chunks stay
in an ignored build directory. Property-boundary evidence comes from the separately
validated schema reader; this script independently checks the entire native window,
bulk selection/ranges, mip bytes and every preview pixel. No engine is launched.
"""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import struct
import subprocess
from PIL import Image

class Wire:
    def __init__(self, data, begin, end):
        self.data, self.pos, self.end = data, begin, end
    def take(self, count):
        assert 0 <= count <= self.end-self.pos
        value = self.data[self.pos:self.pos+count]
        self.pos += count
        return value
    def u32(self): return int.from_bytes(self.take(4), 'little')
    def boolean(self):
        value = self.u32()
        assert value in (0, 1)
        return value != 0
    def name(self, names):
        index, number = self.u32(), self.u32()
        assert number == 0 and index < len(names)
        return names[index]
    def string(self):
        count = self.u32()
        assert 1 <= count <= 64
        raw = self.take(count)
        assert raw[-1] == 0
        return raw[:-1].decode('ascii')

def digest(path):
    with path.open('rb') as stream: return hashlib.file_digest(stream, 'sha256').hexdigest()

def reference_pixels(format, raw, width, height):
    if format in ('PF_DXT1', 'PF_DXT3', 'PF_DXT5'):
        code = {'PF_DXT1': 1, 'PF_DXT3': 2, 'PF_DXT5': 3}[format]
        return Image.frombytes('RGBA', (width,height), raw, 'bcn', (code,)).tobytes(), 3
    if format == 'PF_BC5':
        image = Image.frombytes('RGB',(width,height),raw,'bcn',(5,'BC5'))
        red,green,_ = image.split()
        return Image.merge('RGBA',(red,green,Image.new('L',(width,height)),Image.new('L',(width,height),255))).tobytes(),1
    if format == 'PF_B8G8R8A8': return Image.frombytes('RGBA',(width,height),raw,'raw','BGRA').tobytes(),0
    if format == 'PF_G8': return Image.frombytes('L',(width,height),raw).convert('RGBA').tobytes(),0
    if format == 'PF_FloatRGBA':
        values = struct.unpack('<'+'e'*(len(raw)//2), raw)
        def display(v):
            assert math.isfinite(v)
            linear = max(0,v)/(1+max(0,v))
            srgb = linear*12.92 if linear <= 0.0031308 else 1.055*linear**(1/2.4)-0.055
            return int(min(1,max(0,srgb))*255+0.5)
        return bytes(display(v) if i%4 != 3 else int(min(1,max(0,v))*255+0.5) for i,v in enumerate(values)),0
    raise ValueError('No independent pixel reference for '+format)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('pairs','context','container','script-objects'): parser.add_argument('--'+name,type=Path,required=True)
    parser.add_argument('--codec',type=Path)
    parser.add_argument('--cli',type=Path,default=Path('build-core/Release/cyuediff.exe'))
    parser.add_argument('--output-dir',type=Path,default=Path('build-goal/cooked-texture-validation'))
    args=parser.parse_args()
    workspace=Path(__file__).resolve().parents[1];out=args.output_dir.resolve()
    if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
        parser.error('Reports must stay in an ignored workspace build directory')
    pairs=json.loads(args.pairs.read_text(encoding='utf-8'))
    if not isinstance(pairs,list) or not 1<=len(pairs)<=1000: parser.error('Expected 1 through 1000 explicit pairs')
    cli=str(args.cli.resolve(strict=True));env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
    options=['--cooked-profile','ue5.3','--schema',str(args.context),'--script-objects',str(args.script_objects)]
    codec=['--codec',str(args.codec)] if args.codec else []
    def command(*arguments):
        p=subprocess.run([cli,*map(str,arguments)],env=env,capture_output=True,timeout=120)
        assert p.returncode==0,p.stderr.decode('utf-8','replace')
        return json.loads(p.stdout)
    inputs={args.pairs.resolve(strict=True),args.context.resolve(strict=True)}
    context=json.loads(args.context.read_text(encoding='utf-8'));mapping=Path(context['usmap'])
    inputs.add((mapping if mapping.is_absolute() else args.context.parent/mapping).resolve(strict=True))
    for path in (args.container,args.script_objects):inputs.update(Path(p).resolve(strict=True) for p in command('container',path)['files'])
    if args.codec: inputs.add(args.codec.resolve(strict=True))
    inputs.update(Path(p['source']).resolve(strict=True) for p in pairs if 'source' in p)
    if any(p.is_relative_to(out) or p.parent==out for p in inputs):parser.error('Output overlaps an input')
    out.mkdir(parents=True,exist_ok=True);before={str(p):digest(p) for p in inputs}
    inventory=command('container',args.container)
    entries={e['id']:e for e in inventory['entries'] if not e['deleted']}
    cache={};results=[];mips=channels=external=0;maximum=0
    def chunk(id):
        if id not in cache:
            path=out/(id+'.bin');command('container-read',args.container,id,'--out',path,*codec);cache[id]=path.read_bytes()
        return cache[id]
    try:
        for pair in pairs:
            package=command('zen-inspect',args.container,pair['id'],*options,*codec)
            assert package['name']==pair['name']
            if pair.get('sha256'):assert package['sha256']==pair['sha256']
            report=command('zen-texture',args.container,pair['id'],*options,*codec)
            body=chunk(pair['id']);assert hashlib.sha256(body).hexdigest()==report['packageSha256']
            export=package['exports'][report['exportIndex']];props=export['propertyRead']
            start=export['offset']+props['offset']+props['consumedBytes'];end=export['offset']+export['size']
            assert report['nativeOffset']==start and report['nativeBytes']==end-start
            r=Wire(body,start,end)
            if r.boolean():r.take(16)
            assert r.take(4)==bytes((1,0,1,0)) and r.boolean() and r.boolean()
            platforms=[]
            while True:
                format=r.name(package['names'])
                if format=='None':break
                skip_at=r.pos;skip=int.from_bytes(r.take(8),'little');assert skip>=8
                assert r.take(16)==bytes(16)
                width,height,packed=r.u32(),r.u32(),r.u32();assert packed&0xbfffffff==1
                assert r.string()==format
                if packed&0x40000000:assert r.take(8)==bytes(8)
                first,count=r.u32(),r.u32();assert first<32 and 1<=count<=32-first
                w,h=max(1,width>>first),max(1,height>>first);expected=[]
                for index in range(count):
                    bulk_index=r.u32();bulk=package['bulkData'][bulk_index];flags=bulk['flags'];size=bulk['size']
                    assert not flags&((1<<1)|(1<<5)|(1<<9)|(1<<15))
                    if flags&1:
                        type=3 if flags&(1<<11) else 2;offset=bulk['offset']
                        if flags&(1<<14):
                            if pair['id'][:-2]+'03' in entries:type,offset=3,bulk['duplicateOffset']
                        elif flags&(1<<12):type=4
                        id=pair['id'][:-2]+f'{type:02x}';raw=chunk(id)[offset:offset+size];storage='external';external+=1
                    else:
                        id=pair['id'];offset=r.pos;raw=r.take(size);storage='inline'
                    assert (r.u32(),r.u32(),r.u32())==(w,h,1) and len(raw)==size
                    actual=report['platforms'][len(platforms)]['mips'][index]
                    assert (actual['width'],actual['height'],actual['bulkIndex'],actual['offset'],actual['size'],actual['storage'],actual['chunkId'])==(w,h,bulk_index,offset,size,storage,id)
                    assert actual['sha256']==hashlib.sha256(raw).hexdigest()
                    reference,tolerance=reference_pixels(format,raw,w,h)
                    image=out/(pair['id']+f'-{len(platforms)}-{index}.png')
                    command('zen-preview',args.container,pair['id'],*options,*codec,'--platform',len(platforms),'--mip',index,'--out',image)
                    with Image.open(image) as png:decoded=png.convert('RGBA').tobytes();assert png.size==(w,h)
                    differences=[abs(a-b) for a,b in zip(reference,decoded,strict=True)]
                    delta=max(differences,default=0);assert delta<=tolerance,(format,index,delta)
                    maximum=max(maximum,delta);channels+=len(decoded);mips+=1
                    expected.append({'width':w,'height':h,'bytes':size,'storage':storage,'maximumPixelDelta':delta})
                    w,h=max(1,w//2),max(1,h//2)
                assert not r.boolean() and r.pos==skip_at+skip
                platforms.append({'format':format,'mips':expected})
            assert r.pos==end and len(platforms)==len(report['platforms'])
            results.append({'package':pair['name'],'nativeBytes':end-start,'platforms':platforms})
    finally:
        unchanged=all(digest(Path(p))==value for p,value in before.items())
        summary={'complete':len(results)==len(pairs) and unchanged,'textures':len(results),'mips':mips,'externalMips':external,'channels':channels,'maximumPixelDelta':maximum,'originalsUnchanged':unchanged,'inputs':before,'results':results}
        (out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
        assert unchanged,'Original inputs changed during validation'
    print(json.dumps({k:v for k,v in summary.items() if k not in ('inputs','results')},indent=2))

if __name__=='__main__': main()
