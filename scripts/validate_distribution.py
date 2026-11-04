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
  if path.suffix.lower() in ('.uasset','.umap','.ubulk','.uexp','.uptnl','.upayload','.pak','.utoc','.ucas','.pdb') or 'oodle' in path.name.lower():raise RuntimeError('Host-owned or development data in archive')
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
print('Archive notices, absence of host data, extracted CLI'+(', desktop WARP' if desktop.is_file() else '')+(' and installed Core consumer' if args.consumer else '')+' validated')
print('Validated extraction retained in '+str(target))
