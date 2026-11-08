"""Whole cooked Nanite inspection, focused queries and independently contextualized surface differences."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
cli,fixture=[str(Path(p).resolve()) for p in sys.argv[1:3]]
env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-whole-nanite-',dir=Path.cwd()) as tmp:
    root=Path(tmp)
    subprocess.run([fixture,'--write-fixtures',str(root)],capture_output=True,check=True,env=env,timeout=30)
    if len(sys.argv)>3:
        subprocess.run([str(Path(sys.argv[3]).resolve()),'--write-fixtures',str(root)],capture_output=True,check=True,env=env,timeout=30)
    context=json.loads((root/'context.json').read_text(encoding='utf-8'))
    context['bindings']['/Script/Engine.Unused']={'schema':'Collections','offset':0}
    (root/'after.usmap').write_bytes((root/'native.usmap').read_bytes())
    context['usmap']='after.usmap'
    (root/'after-context.json').write_text(json.dumps(context),encoding='utf-8')
    protected={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry='000000000000000000000001'
    common=['--cooked-profile','ue5.3','--mesh-profile','ue5.3-windows','--nanite-profile','ue5.3-windows','--keep-mobile-min-lod','0','--script-objects',root/'global.utoc','--schema',root/'context.json']
    def run(command,*args,code=0,options=common):
        global checks
        p=subprocess.run([cli,command,*map(str,options),*map(str,args)],env=env,capture_output=True,text=True,encoding='utf-8',timeout=30)
        assert p.returncode==code,(command,p.returncode,p.stderr,p.stdout[:1500])
        checks+=1
        return json.loads(p.stdout) if p.stdout.strip() else p.stderr
    for name,pages in [('resident',1),('inline',2),('external',2)]:
        value=run('zen-nanite',root/(name+'.utoc'),entry)
        assert value['status']=='decoded' and value['geometryKnown'] and len(value['pages'])==pages
        checks+=1
    selected=run('zen-nanite-cluster',root/'external.utoc',entry,'--page','1','--cluster','0')
    assert selected['positions']==[[-2,-2,0],[0,-2,0],[-2,0,0]] and selected['triangles'][0]['material']==5
    checks+=1
    shading=run('zen-nanite-cluster',root/'external.utoc',entry,'--page','1','--cluster','0','--nanite-attributes','1')
    assert shading['naniteContext']['storedAttributesDecoded'] and len(shading['attributes'])==3
    assert all(a['packedNormal']==0 and a['normal']==[0,0,-1] and a['tangent'] is None for a in shading['attributes'])
    checks+=2
    run('zen-nanite',root/'resident.utoc',entry,'--nanite-attributes','2',code=2)
    run('zen-nanite-diff',root/'resident.utoc',entry,root/'resident.utoc',entry,'--nanite-attributes','1','--after-nanite-attributes','0')
    same=run('zen-nanite-diff',root/'resident.utoc',entry,root/'resident.utoc',entry)
    assert same['surfaceStatus']==same['encodingStatus']=='identical' and same['matchedTriangles']==1
    checks+=1
    changed=run('zen-nanite-diff',root/'resident.utoc',entry,root/'changed.utoc',entry,'--after-schema',root/'after-context.json',code=1)
    assert changed['removedTriangles']==changed['addedTriangles']==1 and changed['surfaceStatus']=='changed'
    assert changed['beforeContext']['source']['schemaContextSha256']!=changed['afterContext']['source']['schemaContextSha256']
    checks+=2
    empty=run('zen-nanite',root/'empty.utoc',entry)
    assert empty['status']=='empty' and empty['geometryKnown']
    checks+=1
    removed=run('zen-nanite-diff',root/'resident.utoc',entry,root/'empty.utoc',entry,code=1)
    assert removed['removedTriangles']==1 and removed['addedTriangles']==0 and removed['complete']
    checks+=1
    for name in ('missing','unused'):
        unknown=run('zen-nanite',root/(name+'.utoc'),entry)
        assert not unknown['geometryKnown'] and unknown['status']=='unavailable'
        diff=run('zen-nanite-diff',root/'resident.utoc',entry,root/(name+'.utoc'),entry,code=2)
        assert not diff['complete'] and diff['removedTriangles']==diff['addedTriangles']==0
        checks+=2
    run('zen-nanite-cluster',root/'empty.utoc',entry,code=2)
    run('zen-nanite-cluster',root/'resident.utoc',entry,'--page','1',code=2)
    run('zen-nanite',root/'resident.utoc',entry,'--page','0',code=2)
    run('zen-nanite-diff',root/'resident.utoc',entry,root/'resident.utoc',entry,'--after-lod','0',code=2)
    run('zen-nanite',root/'resident.utoc',entry,'--nanite-profile','guess',code=2)
    run('zen-nanite',root/'resident.utoc',entry,options=common[:common.index('--nanite-profile')]+common[common.index('--keep-mobile-min-lod'):],code=2)
    for name in ('external.utoc','external.ucas','global.utoc','global.ucas','context.json','native.usmap'):
        run('zen-nanite',root/'external.utoc',entry,'--out',root/name,code=2)
    for name in ('changed.utoc','changed.ucas','after-context.json','after.usmap'):
        run('zen-nanite-diff',root/'resident.utoc',entry,root/'changed.utoc',entry,'--after-schema',root/'after-context.json','--out',root/name,code=2)
    if len(sys.argv)>3:
        for name,channels in [('color',['colors']),('uv',['uvs']),('tangent',['tangents']),('normal',['normals','tangents'])]:
            value=run('zen-nanite-diff',root/'attrs-before.utoc',entry,root/('attrs-'+name+'.utoc'),entry,'--compare-nanite-attributes','1',code=1)
            assert value['surfaceStatus']=='identical' and value['attributeStatus']=='changed' and value['modified'][0]['channels']==channels
            checks+=1
        value=run('zen-nanite-diff',root/'attrs-before.utoc',entry,root/'attrs-before.utoc',entry,'--compare-nanite-attributes','1','--after-nanite-attributes','0',code=2)
        assert value['geometryComplete'] and not value['complete'] and value['attributeStatus']=='unavailable'
        checks+=1
        run('zen-nanite',root/'attrs-before.utoc',entry,'--compare-nanite-attributes','1',code=2)
        run('zen-nanite-diff',root/'attrs-before.utoc',entry,root/'attrs-color.utoc',entry,'--compare-nanite-attributes','2',code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in protected.items())
    checks+=1
print(f'{checks} whole cooked Nanite CLI checks; protected inputs unchanged')
