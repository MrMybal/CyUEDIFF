"""Validate a locally built ZIP, its executables and its exported Core SDK."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import tempfile
from zipfile import ZipFile

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('archive',type=Path)
parser.add_argument('--consumer',action='store_true',help='Compile a Core-only consumer against the extracted SDK')
parser.add_argument('--static-runtime',action='store_true',help='Match archives built with the static MSVC runtime')
args=parser.parse_args()
root=Path(__file__).resolve().parents[1]
work=root/'build-native'
work.mkdir(exist_ok=True)
archive=args.archive.resolve(strict=True)
target=Path(tempfile.mkdtemp(prefix='distribution-validation-',dir=work)).resolve()
assert target.is_relative_to(work.resolve())
with ZipFile(archive) as package:
 seen=set();parents=set()
 for entry in package.infolist():
  path=PurePosixPath(entry.filename)
  if path.is_absolute() or '..' in path.parts or '\\' in entry.filename or ':' in entry.filename:raise RuntimeError('Non-portable archive path')
  if not path.parts:raise RuntimeError('Empty archive path')
  key=entry.filename.casefold()
  if key in seen:raise RuntimeError('Duplicate archive path')
  seen.add(key);parents.add(path.parts[0])
  if (entry.external_attr>>16)&0o170000==0o120000:raise RuntimeError('Archive symlink')
  if path.suffix.lower() in ('.uasset','.umap','.ubulk','.uexp','.uptnl','.upayload','.pak','.utoc','.ucas','.usmap','.pdb') or 'oodle' in path.name.lower():raise RuntimeError('Host-owned or development data in archive')
 if len(parents)!=1:raise RuntimeError('Expected one package root')
 package.extractall(target)
prefix=target/next(iter(parents))
for name in ('LICENSE','THIRD_PARTY_NOTICES.md','licenses/nlohmann/LICENSE.MIT','licenses/stb/LICENSE'):
 if not (prefix/'share/CyUEDiff'/name).is_file():raise RuntimeError('Missing notice: '+name)
for file in prefix.rglob('*.cmake'):
 if str(root).replace('\\','/') in file.read_text(encoding='utf-8').replace('\\','/'):raise RuntimeError('SDK exports a source checkout path')
for header in (prefix/'include/cyuediff').glob('*.hpp'):
 if '#include <cyui/' in header.read_text(encoding='utf-8'):raise RuntimeError('Core SDK contains UI-dependent header: '+header.name)

env={key.upper() if os.name=='nt' else key:value for key,value in os.environ.items()}
suffix='.exe' if os.name=='nt' else ''
cli=prefix/'bin'/('cyuediff'+suffix)
if os.name!='nt':cli.chmod(0o755)
result=subprocess.run([str(cli),'--help'],capture_output=True,check=True,env=env,timeout=20)
if b'CyUEDiff' not in result.stdout:raise RuntimeError('CLI did not start')
desktop=prefix/'bin'/('cyuediff_desktop'+suffix)
if desktop.is_file():
 workspace_file=target/'workspace.json';workspace_report=target/'workspace-report.json'
 subprocess.run([str(desktop),'--smoke','--no-preferences','--save-workspace',str(workspace_file),'--report',str(workspace_report)],check=True,env=env,timeout=45)
 saved_workspace=json.loads(workspace_file.read_text(encoding='utf-8'))
 assert saved_workspace['schema']=='cyuediff.workspace/4' and len(saved_workspace['documents'])==1
 saved_workspace['documents'][0]['viewState']={'asset':{'filter':'installed pending state'},'layout':{'sidebarWidth':500,'project':True}}
 workspace_file.write_text(json.dumps(saved_workspace),encoding='utf-8')
 original_workspace=workspace_file.read_bytes()
 subprocess.run([str(desktop),'--smoke','--no-preferences','--workspace',str(workspace_file),'--report',str(workspace_report)],check=True,env=env,timeout=45)
 restored=json.loads(workspace_report.read_text(encoding='utf-8'))
 assert restored['workspaceFile']['loaded'] and restored['document'] is None and workspace_file.read_bytes()==original_workspace
 assert restored['workspaceFile']['context']['documents'][0]['viewState']==saved_workspace['documents'][0]['viewState']
 print('Extracted desktop workspace save/reopen validated')
 if not (prefix/'share/CyUEDiff/licenses/CyUI/LICENSE').is_file():raise RuntimeError('Missing CyUI license')
if args.consumer:
 cmake=shutil.which('cmake')
 if not cmake and os.name=='nt':cmake=str(Path(os.environ.get('ProgramFiles',r'C:\Program Files'))/'Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
 if not cmake:raise RuntimeError('CMake is required for --consumer')
 build=target/'consumer'
 configure=[cmake,'-S',str(root/'examples/catalog_consumer'),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(build),'--config','Release','--parallel','2'],check=True,env=env)
 project=target/'fixture';(project/'Content').mkdir(parents=True)
 (project/'Example.uproject').write_text('{"FileVersion":3}',encoding='utf-8')
 (project/'Content/Test.uasset').write_bytes(b'catalogue metadata only')
 executable=build/'Release'/('catalog_consumer'+suffix) if os.name=='nt' else build/'catalog_consumer'
 result=subprocess.run([str(executable),str(project/'Example.uproject')],capture_output=True,check=True,env=env,timeout=20)
 data=json.loads(result.stdout)
 if not data.get('assets'):raise RuntimeError('Installed Core consumer did not list fixture')
 if not (prefix/'include/cyuediff/container.hpp').is_file():raise RuntimeError('Missing container API in SDK')
 container_build=target/'container-consumer'
 configure=[cmake,'-S',str(root/'examples/container_consumer'),'-B',str(container_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(container_build),'--config','Release','--parallel','2'],check=True,env=env)
 # Original, uncompressed one-chunk TOC fixture, independent of the source checkout at runtime.
 import struct
 header=bytearray(144);header[:16]=b'-==--==--==--==-';header[16]=5
 struct.pack_into('<9I',header,20,144,1,1,12,0,32,4,0,1)
 struct.pack_into('<Q',header,88,0xffffffffffffffff)
 chunk=bytes(11)+bytes([2])+bytes(5)+(4).to_bytes(5,'big')
 block=bytes(5)+(4).to_bytes(3,'little')+(4).to_bytes(3,'little')+bytes(1)
 toc=target/'consumer.utoc';toc.write_bytes(header+chunk+block+bytes(33));toc.with_suffix('.ucas').write_bytes(b'core')
 executable=container_build/'Release'/('container_consumer'+suffix) if os.name=='nt' else container_build/'container_consumer'
 result=subprocess.run([str(executable),str(toc)],capture_output=True,check=True,env=env,timeout=20)
 if json.loads(result.stdout).get('firstMemberSha256')!=hashlib.sha256(b'core').hexdigest():raise RuntimeError('Installed container consumer failed')
 if not (prefix/'include/cyuediff/zen.hpp').is_file():raise RuntimeError('Missing cooked Zen API in SDK')
 zen_build=target/'zen-consumer'
 configure=[cmake,'-S',str(root/'examples/zen_consumer'),'-B',str(zen_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(zen_build),'--config','Release','--parallel','2'],check=True,env=env)
 names=[b'/Game/Consumer',b'Consumer'];u32=lambda v:struct.pack('<I',v);u64=lambda v:struct.pack('<Q',v)
 batch=u32(2)+u32(sum(map(len,names)))+bytes(24)+b''.join(len(n).to_bytes(2,'big') for n in names)+b''.join(names)
 payload=bytearray(52)+batch+u64(0);offsets=[len(payload)]*3
 payload+=u64(0)+u64(4)+u32(1)+u32(0)+u64(0xffffffffffffffff)+u64(0x4000000000000001)+u64(0xffffffffffffffff)*2+u64(0)+u32(3)+bytes(4)
 offsets.append(len(payload));payload+=u32(0)+u32(0)+u32(0)+u32(1)
 offsets.append(len(payload));payload+=bytes(20)
 offsets.extend([len(payload),len(payload)]);payload+=u32(0)
 struct.pack_into('<6I',payload,0,0,len(payload),0,0,0x2000,0)
 struct.pack_into('<7I',payload,24,*offsets);payload+=b'core'
 header=bytearray(144);header[:16]=b'-==--==--==--==-';header[16]=5
 struct.pack_into('<9I',header,20,144,1,1,12,0,32,65536,0,1);struct.pack_into('<Q',header,88,0xffffffffffffffff)
 block=bytes(5)+len(payload).to_bytes(3,'little')*2+bytes(1)
 toc=target/'zen-consumer.utoc';toc.write_bytes(header+bytes(11)+bytes([1])+bytes(5)+len(payload).to_bytes(5,'big')+block+bytes(33));toc.with_suffix('.ucas').write_bytes(payload)
 executable=zen_build/'Release'/('zen_consumer'+suffix) if os.name=='nt' else zen_build/'zen_consumer'
 result=subprocess.run([str(executable),str(toc),'ue5.3'],capture_output=True,check=True,env=env,timeout=20)
 parsed=json.loads(result.stdout);body=parsed['packages'][0]['snapshot']['objects']['/Game/Consumer.Consumer']['opaque']
 if body['sha256']!=hashlib.sha256(b'core').hexdigest():raise RuntimeError('Installed cooked Zen consumer failed')
 if not (prefix/'include/cyuediff/schema.hpp').is_file():raise RuntimeError('Missing USMAP API in SDK')
 schema_build=target/'schema-consumer'
 configure=[cmake,'-S',str(root/'examples/schema_consumer'),'-B',str(schema_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(schema_build),'--config','Release','--parallel','2'],check=True,env=env)
 # Minimal USMAP v0 and one unversioned int, independent of native fixtures.
 payload=u32(2)+bytes([8])+b'Consumer'+bytes([5])+b'Value'+u32(0)+u32(1)
 payload+=u32(0)+struct.pack('<iHHHBiB',-1,1,1,0,1,1,2)
 mappings=target/'consumer.usmap'
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 values=target/'consumer-values.bin';values.write_bytes(struct.pack('<Hi',0x0300,42))
 executable=schema_build/'Release'/('schema_consumer'+suffix) if os.name=='nt' else schema_build/'schema_consumer'
 result=subprocess.run([str(executable),str(mappings),str(values)],capture_output=True,check=True,env=env,timeout=20)
 if json.loads(result.stdout)['Consumer.Value']['value']!=42:raise RuntimeError('Installed USMAP consumer failed')
 context_path=target/'consumer-context.json'
 context_path.write_text(json.dumps({'format':'cyuediff.zen-schema/1','profile':'ue5.3',
                                    'usmap':mappings.name,'sha256':hashlib.sha256(mappings.read_bytes()).hexdigest(),
                                    'bindings':{'/Script/Consumer.Consumer':{'schema':'Consumer','offset':0}}}),encoding='utf-8')
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 if json.loads(result.stdout)['Consumer.Value']['value']!=42:raise RuntimeError('Installed schema context consumer failed')
 # Independently encode CEXT/PPTH/EATR/ENVP for the same external SDK consumer.
 names=[b'Consumer',b'Value',b'/Script/Consumer']
 payload=u32(3)+b''.join(bytes([len(n)])+n for n in names)+u32(0)+u32(1)
 payload+=u32(0)+struct.pack('<iHHHBiB',-1,1,1,0,1,1,2)
 ppth=bytes(1)+u32(0)+u32(1)+u32(2)
 eatr=bytes(1)+u32(0)+u32(1)+bytes([2])+u32(0x40000001)+u32(1)+u64(0x8000000000000010)
 extensions=[(b'PPTH',ppth),(b'EATR',eatr),(b'ENVP',bytes(1)+u32(0))]
 payload+=b'CEXT'+bytes(1)+u32(len(extensions))+b''.join(key+u32(len(value))+value for key,value in extensions)
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 result=subprocess.run([str(executable),str(mappings),str(values)],capture_output=True,check=True,env=env,timeout=20)
 if json.loads(result.stdout)['/Script/Consumer.Consumer.Value']['value']!=42:raise RuntimeError('Installed extended USMAP consumer failed')
 result=subprocess.run([str(cli),'usmap',str(mappings)],capture_output=True,check=True,env=env,timeout=20)
 report=json.loads(result.stdout)
 if report['extensions']!=['PPTH','EATR','ENVP'] or report['schemas'][0]['serializedPropertyFlagsHex']!=['8000000000000010']:raise RuntimeError('Installed extended USMAP metadata failed')
 # Independent unversioned Map<int, Set<int16>> with stored removals/additions.
 payload=u32(2)+bytes([8])+b'Consumer'+bytes([5])+b'Value'+u32(0)+u32(1)
 payload+=u32(0)+struct.pack('<iHHHBi',-1,1,1,0,1,1)+bytes([24,2,25,22])
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 wire=struct.pack('<Hiiii',0x0300,1,7,2,9)+struct.pack('<ihihh',1,4,2,1,2)+struct.pack('<iii',11,0,0)
 values.write_bytes(wire)
 result=subprocess.run([str(executable),str(mappings),str(values)],capture_output=True,check=True,env=env,timeout=20)
 collection=json.loads(result.stdout)['Consumer.Value']['value']
 if collection['removed']!={'7':7} or collection['replace'] or collection['entries']['9']['value']['entries']!={'1':1,'2':2}:raise RuntimeError('Installed unversioned collection consumer failed')
 if collection['entries']['9']['value']['removed']!={'4':4}:raise RuntimeError('Installed unversioned set removal failed')
 if set(collection['entries'])!={'9','11'} or collection['entries']['11']['value']['entries']:raise RuntimeError('Installed unversioned collection tail failed')
 # Independent native struct mapping and version-two context, using only installed SDK headers/libraries.
 native_names=[b'Consumer',b'Value',b'Native']
 payload=u32(3)+b''.join(bytes([len(n)])+n for n in native_names)+u32(0)+u32(2)
 payload+=u32(0)+struct.pack('<iHHHBiBi',-1,1,1,0,1,1,9,2)
 payload+=u32(2)+struct.pack('<iHH',-1,0,0)
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 values.write_bytes(struct.pack('<H10d',0x0300,0,0,0,1,1000000000000.125,2.5,3.5,1,1,1))
 context_path.write_text(json.dumps({'format':'cyuediff.zen-schema/2','profile':'ue5.3',
     'usmap':mappings.name,'sha256':hashlib.sha256(mappings.read_bytes()).hexdigest(),
     'bindings':{'/Script/Consumer.Consumer':{'schema':'Consumer','offset':0}},
     'nativeStructs':{'Native':'transform-f64'}}),encoding='utf-8')
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 native=json.loads(result.stdout)['Consumer.Value']
 if native['nativeLayout']!='transform-f64' or native['value']['translation']!=[1000000000000.125,2.5,3.5]:raise RuntimeError('Installed native transform consumer failed')
 values.write_bytes(struct.pack('<HB',0x0380,1))
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 if json.loads(result.stdout)['Consumer.Value']['value']['scale']!=[0,0,0]:raise RuntimeError('Installed zero native struct consumer failed')
 print('Installed native struct and version-two context consumer validated')
 # Independent text, empty multicast and null weak values in a raw property buffer.
 names=[b'Consumer',b'Value',b'Bindings',b'Weak']
 payload=u32(len(names))+b''.join(bytes([len(n)])+n for n in names)+u32(0)+u32(1)
 payload+=u32(0)+struct.pack('<iHH',-1,3,3)
 for index,kind in enumerate((11,13,14)):payload+=struct.pack('<HBiB',index,1,index+1,kind)
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 def string_wire(text):
  data=text.encode('ascii')+bytes(1)
  return struct.pack('<i',len(data))+data
 values.write_bytes(struct.pack('<HIb',0x0700,4,0)+string_wire('SDK namespace')+string_wire('StableKey')+string_wire('SDK source')+u32(0)+u32(0))
 context_path.write_text(json.dumps({'format':'cyuediff.zen-schema/2','profile':'ue5.3',
     'usmap':mappings.name,'sha256':hashlib.sha256(mappings.read_bytes()).hexdigest(),
     'bindings':{'/Script/Consumer.Consumer':{'schema':'Consumer','offset':0}},'nativeStructs':{}}),encoding='utf-8')
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 typed=json.loads(result.stdout)
 if typed['Consumer.Value']['value']['source']!='SDK source' or typed['Consumer.Value']['value']['key']!='StableKey':raise RuntimeError('Installed text consumer failed')
 if typed['Consumer.Bindings']['value']['bindings'] or typed['Consumer.Weak']['value']['object'] is not None:raise RuntimeError('Installed persistent reference consumer failed')
 source='Bonjour \u00e9';encoded=source.encode('utf-16-le')+bytes(2)
 values.write_bytes(struct.pack('<HIbIi',0x0700,2,-1,1,-len(encoded)//2)+encoded+u32(0)+u32(0))
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 if json.loads(result.stdout)['Consumer.Value']['value']['source']!=source:raise RuntimeError('Installed invariant Unicode text consumer failed')
 print('Installed cooked text and persistent reference consumer validated')
 # Independent optional integers, empty field paths and a set optional field path.
 names=[b'Consumer',b'Value',b'Path',b'Nested']
 payload=u32(len(names))+b''.join(bytes([len(n)])+n for n in names)+u32(0)+u32(1)
 payload+=u32(0)+struct.pack('<iHH',-1,3,3)
 for index,descriptor in enumerate((bytes([28,2]),bytes([27]),bytes([28,27]))):
  payload+=struct.pack('<HBi',index,1,index+1)+descriptor
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 context_path.write_text(json.dumps({'format':'cyuediff.zen-schema/2','profile':'ue5.5',
     'usmap':mappings.name,'sha256':hashlib.sha256(mappings.read_bytes()).hexdigest(),
     'bindings':{'/Script/Consumer.Consumer':{'schema':'Consumer','offset':0}},'nativeStructs':{}}),encoding='utf-8')
 values.write_bytes(struct.pack('<HIiIIIII',0x0700,1,-73,0,0,1,0,0))
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 typed=json.loads(result.stdout)
 if typed['Consumer.Value']['value']!={'kind':'optional','isSet':True,'value':-73}:raise RuntimeError('Installed set optional consumer failed')
 if typed['Consumer.Path']['value']!={'kind':'field-path','path':[],'owner':None}:raise RuntimeError('Installed field path consumer failed')
 if typed['Consumer.Nested']['value']['value']!=typed['Consumer.Path']['value']:raise RuntimeError('Installed optional field path consumer failed')
 values.write_bytes(struct.pack('<HIIII',0x0700,0,0,0,0))
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 typed=json.loads(result.stdout)
 if typed['Consumer.Value']['value']!={'kind':'optional','isSet':False} or typed['Consumer.Nested']['value']!={'kind':'optional','isSet':False}:raise RuntimeError('Installed unset optional consumer failed')
 print('Installed optional and field path SDK consumer validated')
 # Independently encoded recursive text formats and counted strings.
 names=[b'Consumer',b'Value',b'Utf8',b'Ascii']
 payload=u32(len(names))+b''.join(bytes([len(n)])+n for n in names)+u32(0)+u32(1)
 payload+=u32(0)+struct.pack('<iHH',-1,3,3)
 for index,kind in enumerate((11,29,30)):payload+=struct.pack('<HBiB',index,1,index+1,kind)
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 context_path.write_text(json.dumps({'format':'cyuediff.zen-schema/2','profile':'ue5.5',
     'usmap':mappings.name,'sha256':hashlib.sha256(mappings.read_bytes()).hexdigest(),
     'bindings':{'/Script/Consumer.Consumer':{'schema':'Consumer','offset':0}},'nativeStructs':{}}),encoding='utf-8')
 def localized_wire(source):
  return struct.pack('<Ib',0,0)+string_wire('SDK namespace')+string_wire('SDK key')+string_wire(source)
 source='SDK \u00e9';utf8=source.encode('utf-8')
 byte_values=u32(len(utf8))+utf8+u32(9)+b'SDK ASCII'
 nested=struct.pack('<Ib',0,10)+localized_wire('MiXeD SDK')+bytes([1])
 named=struct.pack('<Ib',0,1)+localized_wire('{Count}: {Label}')+u32(2)
 named+=string_wire('Count')+struct.pack('<bq',0,43)+string_wire('Label')+bytes([4])+nested
 values.write_bytes(struct.pack('<H',0x0700)+named+byte_values)
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 typed=json.loads(result.stdout);text=typed['Consumer.Value']['value']
 if text['arguments']['Count']!={'argumentType':'int64','value':43} or text['arguments']['Label']['value']['source']['source']!='MiXeD SDK':raise RuntimeError('Installed recursive named text consumer failed')
 if typed['Consumer.Utf8']['value']!=source or typed['Consumer.Ascii']['value']!='SDK ASCII':raise RuntimeError('Installed counted byte string consumer failed')
 numeric=struct.pack('<Ib',0,6)+string_wire('EUR')+struct.pack('<bd',3,1000000000000.125)+u32(1)
 numeric+=struct.pack('<IIbiiii',1,0,2,1,324,2,4)+string_wire('fr-FR')
 values.write_bytes(struct.pack('<H',0x0700)+numeric+byte_values)
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 text=json.loads(result.stdout)['Consumer.Value']['value']
 if text['currencyCode']!='EUR' or text['source']['value']!=1000000000000.125 or text['formatOptions']['roundingMode']!=2:raise RuntimeError('Installed numeric currency text consumer failed')
 print('Installed recursive text formatting and byte string SDK consumer validated')
 calendar=struct.pack('<Ibqbb',0,9,638980123456789013,5,1)+string_wire('yyyy-MM-dd')+string_wire('UTC')+string_wire('en-US')
 data=struct.pack('<Ib',0,3)+localized_wire('{Tick}: {Date}')+u32(3)
 data+=string_wire('Tick')+struct.pack('<bq',0,-9007199254740993)
 data+=string_wire('Tick')+struct.pack('<bB',5,255)
 data+=string_wire('Date')+bytes([4])+calendar
 values.write_bytes(struct.pack('<H',0x0700)+data+byte_values)
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
 text=json.loads(result.stdout)['Consumer.Value']['value']
 if text['arguments'][0]['value']!=-9007199254740993 or text['arguments'][1]['value']!=255:raise RuntimeError('Installed argument-data width/gender consumer failed')
 date=text['arguments'][2]['value']
 if date['ticks']!=638980123456789013 or date['customPattern']!='yyyy-MM-dd' or date['timeZone']!='UTC':raise RuntimeError('Installed calendar history consumer failed')
 names_path=target/'consumer-names.json'
 names_path.write_text(json.dumps(['None','SDK generator']),encoding='utf-8')
 contents=bytes([0,1,255,7])
 generator=struct.pack('<Ibiii',0,12,1,0,len(contents))+contents
 values.write_bytes(struct.pack('<H',0x0700)+generator+byte_values)
 result=subprocess.run([str(executable),str(mappings),str(values),str(context_path),str(names_path)],capture_output=True,check=True,env=env,timeout=20)
 value=json.loads(result.stdout)['Consumer.Value']['value']
 if value['generator']!='SDK generator' or value['contents']!={'bytes':4,'sha256':hashlib.sha256(contents).hexdigest()}:raise RuntimeError('Installed opaque generator consumer failed')
 for invalid in ({},[3],['None']):
  names_path.write_text(json.dumps(invalid),encoding='utf-8')
  refused=subprocess.run([str(executable),str(mappings),str(values),str(context_path),str(names_path)],capture_output=True,env=env,timeout=20)
  if refused.returncode!=2:raise RuntimeError('Installed invalid package names were accepted')
 print('Installed explicit name-table and opaque generator consumer validated')
 # Reuse the independent native mapping with new explicit wire tokens.
 payload=u32(3)+b''.join(bytes([len(n)])+n for n in native_names)+u32(0)+u32(2)
 payload+=u32(0)+struct.pack('<iHHHBiBi',-1,1,1,0,1,1,9,2)
 payload+=u32(2)+struct.pack('<iHH',-1,0,0)
 mappings.write_bytes(struct.pack('<HBBII',0x30c4,0,0,len(payload),len(payload))+payload)
 for token,wire,expected in (
     ('date-time-i64',struct.pack('<q',9007199254740993),{'ticks':9007199254740993}),
     ('timespan-i64',struct.pack('<q',-9007199254740993),{'ticks':-9007199254740993}),
     ('box3-f64',struct.pack('<6dB',1,2,3,4,5,6,255),{'min':[1,2,3],'max':[4,5,6],'isValid':255}),
     ('sphere-f32',struct.pack('<4f',1,2,3,-4),{'center':[1,2,3],'radius':-4}),
     ('box-sphere-f64',struct.pack('<7d',1,2,3,4,5,6,7),{'origin':[1,2,3],'boxExtent':[4,5,6],'sphereRadius':7}),
     ('matrix4-f64',struct.pack('<16d',*range(16)),[list(range(i,i+4)) for i in range(0,16,4)]),
     ('plane-f32',struct.pack('<4f',1,2,3,-4),{'normal':[1,2,3],'distance':-4})):
  context_path.write_text(json.dumps({'format':'cyuediff.zen-schema/2','profile':'ue5.5',
      'usmap':mappings.name,'sha256':hashlib.sha256(mappings.read_bytes()).hexdigest(),
      'bindings':{'/Script/Consumer.Consumer':{'schema':'Consumer','offset':0}},'nativeStructs':{'Native':token}}),encoding='utf-8')
  values.write_bytes(struct.pack('<H',0x0300)+wire)
  result=subprocess.run([str(executable),str(mappings),str(values),str(context_path)],capture_output=True,check=True,env=env,timeout=20)
  native=json.loads(result.stdout)['Consumer.Value']
  if native['value']!=expected or native['nativeLayout']!=token:raise RuntimeError('Installed native '+token+' consumer failed')
 print('Installed calendar/argument-data and native temporal/bounds SDK consumers validated')

 image_build=target/'image-consumer'
 configure=[cmake,'-S',str(root/'examples/image_consumer'),'-B',str(image_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(image_build),'--config','Release','--parallel','2'],check=True,env=env)
 image_executable=image_build/'Release'/('image_consumer'+suffix) if os.name=='nt' else image_build/'image_consumer'
 result=subprocess.run([str(image_executable)],capture_output=True,check=True,env=env,timeout=20)
 image_data=json.loads(result.stdout)
 for kind,bounds in (('ldr',{'x':1,'y':0,'width':1,'height':1}),('hdr',{'x':0,'y':1,'width':1,'height':1})):
  if image_data[kind]['changedPixels']!=1 or image_data[kind]['changedBounds']!=bounds:raise RuntimeError('Installed '+kind+' changed bounds failed')
 print('Installed independent LDR/HDR image SDK consumer validated')

 collision_build=target/'collision-consumer'
 configure=[cmake,'-S',str(root/'examples/collision_consumer'),'-B',str(collision_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(collision_build),'--config','Release','--parallel','2'],check=True,env=env)
 collision_executable=collision_build/'Release'/('collision_consumer'+suffix) if os.name=='nt' else collision_build/'collision_consumer'
 result=subprocess.run([str(collision_executable)],capture_output=True,check=True,env=env,timeout=20)
 collision_data=json.loads(result.stdout)
 if not collision_data['snapshot']['complete'] or collision_data['comparison']['status']!='changed' or collision_data['comparison']['changes'][0]['path']!='/bodies/Body/shapes/sphere:name:Ball/geometry/radius':raise RuntimeError('Installed collision SDK projection/diff failed')
 wireframe=collision_data['wireframe']
 if wireframe['space']!='body-local' or len(wireframe['shapes'])!=1 or not wireframe['shapes'][0]['ready'] or len(wireframe['shapes'][0]['lines'])!=96:raise RuntimeError('Installed collision wireframe API failed')
 print('Installed independent collision Core consumer validated')

 skeleton_build=target/'skeleton-consumer'
 configure=[cmake,'-S',str(root/'examples/skeleton_consumer'),'-B',str(skeleton_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(skeleton_build),'--config','Release','--parallel','2'],check=True,env=env)
 skeleton_executable=skeleton_build/'Release'/('skeleton_consumer'+suffix) if os.name=='nt' else skeleton_build/'skeleton_consumer'
 result=subprocess.run([str(skeleton_executable)],capture_output=True,check=True,env=env,timeout=20)
 skeleton_data=json.loads(result.stdout)
 if not skeleton_data['snapshot']['complete'] or skeleton_data['comparison']['status']!='changed' or skeleton_data['pose']['joints']['arm']['position']!=[10.,0.,0.]:raise RuntimeError('Installed skeleton SDK consumer failed')
 if skeleton_data['poseComparison']['status']!='changed' or skeleton_data['poseComparison']['joints']['arm']['delta']!=[2.,0.,0.]:raise RuntimeError('Installed reference displacement comparison failed')
 if skeleton_data['retargetPose']['retargetSource']!='T_Pose' or skeleton_data['retargetPose']['joints']['arm']['position']!=[15.,0.,0.]:raise RuntimeError('Installed named retarget pose failed')
 if skeleton_data['retargetAgainstRaw']['joints']['arm']['delta']!=[-5.,0.,0.]:raise RuntimeError('Installed independent after source selection failed')
 if skeleton_data['unverifiedNativeTail']['status']!='incomplete':raise RuntimeError('Unhashed native tail falsely establishes equality')
 animation=skeleton_data['animationSnapshot']
 if not animation['complete'] or animation['skeletons']['Skeleton']['syncMarkers']['names']['Left']['occurrences']!=2:raise RuntimeError('Installed named animation metadata projection failed')
 animation_diff=skeleton_data['animationComparison']
 if animation_diff['status']!='changed' or len(animation_diff['changes'])!=1 or animation_diff['changes'][0]['path']!='/skeletons/Skeleton/curveMetadata/containers/AnimationCurves/Smile/maxLOD':raise RuntimeError('Installed named animation metadata comparison failed')
 if skeleton_data['strippedAnimationComparison']['status']!='incomplete':raise RuntimeError('Stripped marker metadata falsely establishes equality')
 if not skeleton_data['attachmentSnapshot']['complete'] or skeleton_data['attachmentComparison']['status']!='changed':raise RuntimeError('Installed attachment Core projection failed')
 if skeleton_data['socketPose']['sockets']['Skeleton.Grip']['position']!=[11.,0.,0.] or skeleton_data['socketPoseComparison']['sockets']['Skeleton.Grip']['delta']!=[3.,0.,0.]:raise RuntimeError('Installed socket affine origin comparison failed')
 if skeleton_data['missingSocketOrigin']['complete']:raise RuntimeError('Omitted socket location was incorrectly defaulted')
 print('Installed independent skeleton/animation/attachment/socket origin Core consumer validated')
 animation_build=target/'animation-consumer'
 configure=[cmake,'-S',str(root/'examples/animation_consumer'),'-B',str(animation_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(animation_build),'--config','Release','--parallel','2'],check=True,env=env)
 animation_executable=animation_build/'Release'/('animation_consumer'+suffix) if os.name=='nt' else animation_build/'animation_consumer'
 result=subprocess.run([str(animation_executable)],capture_output=True,check=True,env=env,timeout=20)
 source=json.loads(result.stdout)
 if not source['snapshot']['complete'] or source['comparison']['status']!='changed' or len(source['comparison']['changes'])!=1:raise RuntimeError('Installed animation source comparison failed')
 if source['frame']['tracks']['name:root']['positions']!=[3.,2.,1.] or source['frame']['timeSeconds']!=1./24.:raise RuntimeError('Installed animation source frame failed')
 if source['frameComparison']['status']!='changed' or source['frameComparison']['changedTracks']!=1 or source['frameComparison']['tracks']['name:root']['metrics']['positionDistance']!=2.:raise RuntimeError('Installed local animation frame comparison failed')
 if source['independentFrameComparison']['beforeFrame']!=0 or source['independentFrameComparison']['afterFrame']!=1 or source['independentFrameComparison']['status']!='changed':raise RuntimeError('Installed independent animation frame comparison failed')
 print('Installed independent animation source Core consumer validated')
 pose_build=target/'animation-pose-consumer'
 configure=[cmake,'-S',str(root/'examples/animation_pose_consumer'),'-B',str(pose_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(pose_build),'--config','Release','--parallel','2'],check=True,env=env)
 pose_executable=pose_build/'Release'/('animation_pose_consumer'+suffix) if os.name=='nt' else pose_build/'animation_pose_consumer'
 result=subprocess.run([str(pose_executable)],capture_output=True,check=True,env=env,timeout=20)
 pose_data=json.loads(result.stdout)
 if not pose_data['before']['complete'] or pose_data['before']['joints']['arm']['position']!=[10.,0.,0.] or pose_data['after']['joints']['arm']['position']!=[13.,4.,0.]:raise RuntimeError('Installed explicit animation hierarchy pose failed')
 if pose_data['comparison']['status']!='changed' or pose_data['comparison']['changedJoints']!=2 or pose_data['comparison']['joints']['arm']['distance']!=5.:raise RuntimeError('Installed source animation pose comparison failed')
 if pose_data['selfComparison']['status']!='identical':raise RuntimeError('Installed source animation pose self comparison failed')
 print('Installed independent Skeleton-bound animation pose Core consumer validated')
 curve_build=target/'curve-consumer'
 configure=[cmake,'-S',str(root/'examples/curve_consumer'),'-B',str(curve_build),'-DCMAKE_BUILD_TYPE=Release','-DCMAKE_PREFIX_PATH='+str(prefix)]
 if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
 subprocess.run(configure,check=True,env=env)
 subprocess.run([cmake,'--build',str(curve_build),'--config','Release','--parallel','2'],check=True,env=env)
 curve_executable=curve_build/'Release'/('curve_consumer'+suffix) if os.name=='nt' else curve_build/'curve_consumer'
 result=subprocess.run([str(curve_executable)],capture_output=True,check=True,env=env,timeout=20)
 curves=json.loads(result.stdout)
 if not curves['snapshot']['complete'] or curves['comparison']['status']!='changed' or len(curves['comparison']['changes'])!=1:raise RuntimeError('Installed rich curve comparison failed')
 if not curves['value']['ready'] or curves['value']['value']!=4.:raise RuntimeError('Installed rich curve evaluation failed')
 if curves['sampledComparison']['status']!='changed' or curves['sampledComparison']['changedSamples']!=4 or curves['sampledComparison']['maximumAbsoluteDelta']!=2.:raise RuntimeError('Installed rich curve sampled comparison failed')
 if curves['components']['values']['value']!=4.:raise RuntimeError('Installed scalar component evaluation failed')
 result=subprocess.run([str(curve_executable),'--transform','name:Root'],capture_output=True,check=True,env=env,timeout=20)
 transform=json.loads(result.stdout)
 if not transform['components']['ready'] or len(transform['components']['values'])!=9 or transform['components']['values']['rotation.y']!=4.:raise RuntimeError('Installed transform component evaluation failed')
 if transform['comparison']['status']!='changed' or len(transform['comparison']['changes'])!=1 or transform['sampledComparison']['maximumAbsoluteDelta']!=2.:raise RuntimeError('Installed transform sampled comparison failed')
 print('Installed independent rich curve Core consumer validated')



 if (prefix/'include/cyuediff/git.hpp').is_file():
  git_build=target/'git-consumer'
  configure=[cmake,'-S',str(root/'examples/git_consumer'),'-B',str(git_build),'-DCMAKE_PREFIX_PATH='+str(prefix)]
  if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
  subprocess.run(configure,check=True,env=env)
  subprocess.run([cmake,'--build',str(git_build),'--config','Release','--parallel','2'],check=True,env=env)
  git_executable=git_build/'Release'/('git_compare'+suffix) if os.name=='nt' else git_build/'git_compare'
  result=subprocess.run([str(git_executable),str(root),'HEAD'],capture_output=True,check=True,env=env,timeout=30)
  history=json.loads(result.stdout)
  if history.get('schema')!='cyuediff.git-history/1' or not history.get('commits') or history['commits'][0]['id']!=history['head']:raise RuntimeError('Installed Git history consumer failed')
  print('Installed Git SDK history consumer validated')
  git_pose_build=target/'git-animation-pose-consumer'
  configure=[cmake,'-S',str(root/'examples/git_animation_pose_consumer'),'-B',str(git_pose_build),'-DCMAKE_PREFIX_PATH='+str(prefix)]
  if args.static_runtime:configure.append('-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded')
  subprocess.run(configure,check=True,env=env)
  subprocess.run([cmake,'--build',str(git_pose_build),'--config','Release','--parallel','2'],check=True,env=env)
  git_pose_executable=git_pose_build/'Release'/('git_animation_pose'+suffix) if os.name=='nt' else git_pose_build/'git_animation_pose'
  result=subprocess.run([str(git_pose_executable),'--help'],capture_output=True,check=True,env=env,timeout=20)
  if b'BEFORE_SKELETON AFTER_SKELETON' not in result.stdout:raise RuntimeError('Installed Git animation pose consumer failed to start')
  print('Installed Git-bound animation pose SDK consumer compiled and started')



print('Archive notices, absence of host data, extracted CLI'+(', desktop WARP' if desktop.is_file() else '')+(' and installed catalogue/container/cooked/schema/image/collision/skeleton/animation/animation-pose/curve consumers' if args.consumer else '')+' validated')
print('Validated extraction retained in '+str(target))
