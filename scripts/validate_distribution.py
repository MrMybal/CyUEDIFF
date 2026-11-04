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
env={key.upper() if os.name=='nt' else key:value for key,value in os.environ.items()}
suffix='.exe' if os.name=='nt' else ''
cli=prefix/'bin'/('cyuediff'+suffix)
if os.name!='nt':cli.chmod(0o755)
result=subprocess.run([str(cli),'--help'],capture_output=True,check=True,env=env,timeout=20)
if b'CyUEDiff' not in result.stdout:raise RuntimeError('CLI did not start')
desktop=prefix/'bin'/('cyuediff_desktop'+suffix)
if desktop.is_file():
 subprocess.run([str(desktop),'--smoke'],check=True,env=env,timeout=45)
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



print('Archive notices, absence of host data, extracted CLI'+(', desktop WARP' if desktop.is_file() else '')+(' and installed catalogue/container/cooked/schema consumers' if args.consumer else '')+' validated')
print('Validated extraction retained in '+str(target))
