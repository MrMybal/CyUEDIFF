"""Append authentic cooked attribute captures from original fixtures; preserve other images."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
ROOT=Path(__file__).resolve().parents[2]
VERSION=re.search(r'project\(CyUEDiff VERSION (\d+\.\d+\.\d+)',(ROOT/'CMakeLists.txt').read_text(encoding='utf-8'))[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--build',type=Path,default=ROOT/'build-native')
args=parser.parse_args()
binary=args.build.resolve()/'Release';work=ROOT/'build-site/captures';samples=work/'attributes';samples.mkdir(parents=True,exist_ok=True)
env={k.upper():v for k,v in os.environ.items()};env['LOCALAPPDATA']=str(work/'unused-profile')
def run(command):
    result=subprocess.run(list(map(str,command)),cwd=work,env=env,capture_output=True,timeout=90)
    if result.returncode:raise RuntimeError(result.stderr.decode('utf-8','replace'))
    return result.stdout
assert run([binary/'cyuediff.exe','--version']).decode('utf-8').strip()==VERSION,'Build the current source version before capturing'
for name in ['cooked_skeletal','cooked_vertex_attribute']:
    run([binary/('cyuediff_'+name+'_tests.exe'),'--write-fixtures',samples])
path=ROOT/'site/assets/capture-manifest.json';manifest=json.loads(path.read_text(encoding='utf-8'))
assert manifest['privateProjectData'] is False
for item in manifest['records']:item.setdefault('version',manifest['version'])
for locale in ['en','fr']:
    name='cooked-attributes-'+locale;destination=ROOT/'site/assets/screenshots'/(name+'.png')
    flags=['--cooked-profile','ue5.3','--script-objects','attributes/global.utoc','--schema','attributes/context.json','--skeletal-profile','ue5.3-windows','--skeletal-keep-mobile-min-lod','0','--has-vertex-colors','0','--after-cooked-profile','ue5.3','--after-script-objects','attributes/global.utoc','--after-schema','attributes/context.json','--after-skeletal-profile','ue5.3-windows','--after-skeletal-keep-mobile-min-lod','0','--after-has-vertex-colors','0']
    state={'layout':{'sidebarWidth':260,'skeletal-native':False,'schema':False},'container':{'mesh':'/Game/Texture.Texture LOD 0','meshAttributes':True,'meshAttributeView':{'channel':'Texture','vertex':0,'changedOnly':True,'properties':{'selected':'/value/0'}}}}
    report=work/(name+'.json')
    run([binary/'cyuediff_desktop.exe','--smoke','--no-preferences','--no-restore-session','--locale',locale,'--container','attributes/attributes.utoc','--container-entry','000000000000000000000001','--after-container','attributes/attributes-changed.utoc','--after-container-entry','000000000000000000000001','--container-tab','meshes','--capture',destination,'--report',report,*flags,'--document-action','view-state:'+json.dumps(state,separators=(',',':'))])
    data=json.loads(report.read_text(encoding='utf-8'))
    assert data['version']==VERSION and data['containerView']['meshViews'][0]['attributes']['modified']==1
    manifest['records']=[x for x in manifest['records'] if x['file']!=destination.name]
    manifest['records'].append({'file':destination.name,'version':VERSION,'origin':'Windows desktop with original encoded cooked skeletal vertex attribute fixtures','sha256':hashlib.sha256(destination.read_bytes()).hexdigest()})
manifest['version']=VERSION
path.write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8',newline='\n')
print('Prepared two original-format cooked attribute captures; other images preserved')
