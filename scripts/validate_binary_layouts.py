"""Compare installed Core readers, CLI reports and independent Python wire/codepage values."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
for name in ('cli','consumer','layout-fixture','schema-fixture','encoding-fixture'):
    parser.add_argument('--'+name,type=Path,required=True)
parser.add_argument('--output-dir',type=Path,default=Path('build-native/layout-validation'))
args=parser.parse_args()
workspace=Path(__file__).resolve().parents[1]
out=args.output_dir.resolve()
if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
    parser.error('Use an ignored workspace build directory')
out.mkdir(parents=True,exist_ok=True)
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
cli=args.cli.resolve(strict=True);consumer=args.consumer.resolve(strict=True)
fixtures=out/'fixtures';fixtures.mkdir(exist_ok=True)
for generator in (args.layout_fixture,args.schema_fixture,args.encoding_fixture):
    subprocess.run([str(generator.resolve(strict=True)),'--emit-fixtures',str(fixtures)],check=True,env=env,capture_output=True,timeout=40)
hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in fixtures.iterdir() if p.is_file()}
checks=0

def run(exe,*arguments,code=0):
    global checks
    result=subprocess.run([str(exe),*map(str,arguments)],env=env,capture_output=True,timeout=45)
    if result.returncode!=code:
        raise RuntimeError((result.returncode,arguments,result.stderr.decode('utf-8','replace')))
    checks+=1
    return json.loads(result.stdout) if code==0 or code==1 else None

class ReferenceReader:
    def __init__(self,data):self.data=data;self.at=0
    def read(self,count):
        if count<0 or count>len(self.data)-self.at:raise ValueError('truncated reference value')
        result=self.data[self.at:self.at+count];self.at+=count;return result
    def scalar(self,fmt):return struct.unpack('<'+fmt,self.read(struct.calcsize('<'+fmt)))[0]
    def node(self,node,definitions):
        kind=node['type']
        formats={'u8':'B','i8':'b','u16':'H','i16':'h','u32':'I','i32':'i','u64':'Q','i64':'q','f32':'f','f64':'d'}
        if kind in formats:return self.scalar(formats[kind])
        if kind=='ref':return self.node(definitions[node['layout']],definitions)
        if kind=='record':return {field['name']:self.node(field['value'],definitions) for field in node['fields']}
        if kind in ('bool8','bool32'):
            value=self.scalar('B' if kind=='bool8' else 'I')
            if value not in (0,1):raise ValueError('invalid boolean')
            return bool(value)
        if kind=='optional':
            value={'isSet':self.node({'type':node['presence']},definitions)}
            if value['isSet']:value['value']=self.node(node['value'],definitions)
            return value
        if kind in ('array','bytes'):
            count=self.scalar('i') if node['count']=='i32' else node['count']
            if count<0:raise ValueError('negative reference count')
            if kind=='bytes':return self.read(count).hex()
            return [self.node(node['element'],definitions) for _ in range(count)]
        if kind=='string':
            count=self.scalar('i')
            if count==0:return ''
            data=self.read(abs(count)*(2 if count<0 else 1))
            terminator=b'\0\0' if count<0 else b'\0'
            if not data.endswith(terminator):raise ValueError('unterminated reference string')
            return data[:-len(terminator)].decode('utf-16le' if count<0 else node.get('encoding','ascii'))
        raise ValueError('unsupported reference fixture type')

assert run(consumer)['verified']
layout=fixtures/'layouts.json';definitions=json.loads(layout.read_text(encoding='utf-8'))['layouts']
wire_cases=[]
for stem in ('before','after'):
    path=fixtures/(stem+'.bin');raw=path.read_bytes();reference=ReferenceReader(raw)
    expected=reference.node(definitions['Record'],definitions)
    actual=run(consumer,'raw',layout,path)
    assert actual['value']==expected and actual['consumedBytes']==reference.at==len(raw) and actual['remainingBytes']==0
    wire_cases.append({'case':stem,'bytes':len(raw),'sha256':hashlib.sha256(raw).hexdigest(),'value':expected})
# Every truncated prefix is independently refused by both readers.
raw=(fixtures/'before.bin').read_bytes();truncated=out/'truncated.bin'
for length in range(len(raw)):
    sample=raw[:length];truncated.write_bytes(sample)
    try:ReferenceReader(sample).node(definitions['Record'],definitions)
    except (ValueError,struct.error,UnicodeError):pass
    else:raise RuntimeError('Reference reader accepted a truncated fixture')
    run(consumer,'raw',layout,truncated,code=2)

before=fixtures/'before.uasset';after=fixtures/'after.uasset'
public_before=run(cli,'inspect',before,'--layouts',layout)
public_diff=run(cli,'diff',before,after,'--layouts',layout,code=1)
sdk=run(consumer,'package',layout,before,layout,after)
assert sdk['before']=={k:v for k,v in public_before.items() if k!='layoutContexts'}
assert sdk['comparison']['changes']==public_diff['changes']
assert sdk['before']['snapshot']['objects']['TestAsset']['properties']['Native']['value']['data']==wire_cases[0]['value']
entry='000000000000000000000001';cooked_cases=[]
for profile,suffix in [('ue5.3',''),('ue5.5','55')]:
    context=fixtures/f'described-{profile}.json';toc=fixtures/f'native{suffix}.utoc';global_toc=fixtures/'global.utoc'
    public=run(cli,'zen-inspect',toc,entry,'--cooked-profile',profile,'--script-objects',global_toc,'--schema',context)
    sdk=run(consumer,'cooked',context,toc,entry,global_toc)
    assert public==sdk
    assert sdk['exports'][0]['propertyRead']['properties']['/Script/Test.Collections.Lookup']['value']['translation'][0]==1000000000000.125
    cooked_cases.append({'profile':profile,'sha256':sdk['sha256'],'decodedExports':sdk['propertyExportsDecoded']})
codepages=[]
for encoding,codec,raw in [('ascii','ascii',bytes(range(128))),('latin1','latin1',bytes(range(256))),
                          ('windows-1252','cp1252',bytes(b for b in range(256) if b not in (0x81,0x8d,0x8f,0x90,0x9d))),
                          ('utf8','utf-8','Français Ελληνικά 日本語 😀'.encode('utf-8'))]:
    path=out/(encoding+'.bin');path.write_bytes(raw)
    converted=run(consumer,'encoding',encoding,path)['value']
    assert converted==raw.decode(codec)
    codepages.append({'encoding':encoding,'inputBytes':len(raw),'decodedBytes':len(converted.encode('utf-8'))})
for encoding,raw in [('ascii',b'\xff'),('utf8',b'\xed\xa0\x80'),('windows-1252',b'\x81')]:
    path=out/'invalid-encoding.bin';path.write_bytes(raw);run(consumer,'encoding',encoding,path,code=2)
encoded=run(consumer,'package',fixtures/'encoded-layouts.json',fixtures/'encoded.uasset')
assert encoded['before']['snapshot']['objects']['TestAsset']['properties']['EncodingText']['value']==b'caf\xe9 \x80'.decode('cp1252')
assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items())
report={'checks':checks,'originalFixtureFilesUnchanged':len(hashes),'wireCases':wire_cases,
        'truncatedPrefixesRejected':len(raw),'cookedCases':cooked_cases,'codepages':codepages,
        'scope':'Original bounded fixtures and public installed Core/CLI parity; no claim of real plugin layout or matching-build USMAP coverage.'}
report['truncatedPrefixesRejected']=len((fixtures/'before.bin').read_bytes())
(out/'summary.json').write_text(json.dumps(report,indent=2,ensure_ascii=False),encoding='utf-8')
print(json.dumps({'checks':checks,'wireCases':len(wire_cases),'cookedCases':len(cooked_cases),'codepages':len(codepages),'unchangedFixtureFiles':len(hashes)}))
