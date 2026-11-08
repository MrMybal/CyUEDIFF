"""Validate generated native descriptions against an independent cooked wire fixture."""
import copy
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'scripts'))
from uht_mappings import build_cooked_mappings
from validate_mapped_values import norm, flat
cli, fixture = [str(Path(p).resolve()) for p in sys.argv[1:]]
checks = 0
loose_values={'Samples':{'type':{'name':'FloatProperty'},'value':1.25},
              'Material':{'type':{'name':'ObjectProperty'},'value':'/Game/Material.Material'}}
cooked_values={'/Script/Test.Item.Samples[0]':{'type':'FloatProperty','schemaIndex':0,'value':1.25},
               '/Script/Test.Item.Material':{'type':'ObjectProperty','value':{'kind':'package','package':'/Game/Material'}}}
assert flat(norm(loose_values))==flat(norm(cooked_values));checks+=1
changed=json.loads(json.dumps(cooked_values));changed['/Script/Test.Item.Samples[0]']['type']='DoubleProperty'
assert flat(norm(loose_values))!=flat(norm(changed));checks+=1
collision=dict(cooked_values,**{'Samples':{'type':'FloatProperty','value':2}})
try:norm(collision)
except ValueError:checks+=1
else:raise AssertionError('Ambiguous property names collapsed')
assert flat(norm({'Ordered':{'type':'ArrayProperty','value':[1,2]}}))!=flat(norm({'Ordered':{'type':'ArrayProperty','value':[2,1]}}));checks+=1


def property(name, kind, **facts):
    return dict(EngineName=name, SourceName=name, EngineClassName=kind+'Property', Children=[], **facts)

def declaration(name, fields, parent=''):
    return dict(EngineName=name, SourceName='U'+name, EngineClassName='Class', Super=parent, Children=fields)

with tempfile.TemporaryDirectory(prefix='.test-tmp-uht-mapping-', dir=Path.cwd()) as temporary:
    root=Path(temporary);metadata=root/'metadata';metadata.mkdir()
    subprocess.run([fixture,'--write-fixtures',str(root)],capture_output=True,check=True,timeout=20)
    originals={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir() if p.is_file()}
    fields=[property('EditorValue','Float',PropertyFlags='EditorOnly'),
            property('Health','Int'),property('Values','Array',ValueProperty=property('Values','Int16')),
            property('Target','ObjectPtr'),property('Label','Name'),property('Title','Str'),
            property('Samples','Float',ArrayDimensions='2'),
            property('Mode','Enum',Enum='EMode',UnderlyingType='Uint8')]
    enum=dict(EngineClassName='Enum',SourceName='EMode',EngineName='ModeEnum',Children=[],
              EnumValues=[{'Name':'Off','Value':0},{'Name':'On','Value':7}])
    package=dict(schema='cyuediff.uht/1',EngineClassName='Package',SourceName='/Script/Test',EngineName='/Script/Test',
                 Children=[declaration('Base',[property('Enabled','Bool')]), declaration('DemoAsset',fields,'UBase'),enum,
                           declaration('Sparse',[property('First','Int'),property('Opaque','PluginNative'),property('Last','Float')]),
                           declaration('Unresolved',[property('Array','Int',ArrayDimensions='UNKNOWN_COUNT')]),
                           declaration('Derived',[],'UUnresolved'),
                           declaration('Octal',[property('Array','Int',ArrayDimensions='010')])])
    source=metadata/'Test.cyuediff.json'
    source.write_text(json.dumps(package),encoding='utf-8')
    mapping=root/'generated.usmap';report=build_cooked_mappings(metadata,mapping)
    assert report['schemas']==3 and len(report['excludedClasses'])==3
    assert len(report['unsupportedDescriptors'])==1 and report['unsupportedDescriptors'][0]['index']==1
    checks+=2
    first=mapping.read_bytes();build_cooked_mappings(metadata,mapping)
    assert mapping.read_bytes()==first;checks+=1
    def run(*arguments,code=0):
        global checks
        result=subprocess.run([cli,*map(str,arguments)],capture_output=True,timeout=30)
        assert result.returncode==code,result.stderr.decode('utf-8','replace')
        checks+=1
        return json.loads(result.stdout if code!=2 else result.stderr)
    inspected=run('usmap',mapping)
    schemas={s['path']:s for s in inspected['schemas']}
    assert schemas['/Script/Test.DemoAsset']['slots']==8
    assert schemas['/Script/Test.DemoAsset']['super']=='/Script/Test.Base'
    assert schemas['/Script/Test.Sparse']['slots']==3 and schemas['/Script/Test.Sparse']['mappedSlots']==2
    assert set(schemas)=={'/Script/Test.DemoAsset','/Script/Test.Base','/Script/Test.Sparse'}
    checks+=4
    context=root/'generated.json'
    context.write_text(json.dumps({'format':'cyuediff.zen-schema/1','profile':'ue5.3',
        'usmap':mapping.name,'sha256':hashlib.sha256(first).hexdigest(),
        'bindings':{'/Script/Test.DemoAsset':{'schema':'/Script/Test.DemoAsset','offset':0}}}),encoding='utf-8')
    options=['--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',context]
    before=run('zen-inspect',root/'schema.utoc','000000000000000000000001',*options)
    assert before['propertyExportsDecoded']==2 and before['propertyExportsFailed']==0
    properties=before['exports'][0]['propertyRead']['properties']
    assert properties['/Script/Test.DemoAsset.Health']['value']==42
    assert properties['/Script/Test.DemoAsset.Values']['value']==[-12,34]
    assert properties['/Script/Test.DemoAsset.Label']['value']=='Example_2'
    assert properties['/Script/Test.DemoAsset.Samples[0]']['serializedZero'] is True
    assert properties['/Script/Test.Base.Enabled']['value'] is True
    assert before['exports'][0]['propertyRead']['mappingVerified'] is False
    checks+=7
    delta=run('zen-diff',root/'schema.utoc','000000000000000000000001',root/'changed.utoc','000000000000000000000001',*options,code=1)
    assert any(c['path'].endswith('/properties/~1Script~1Test.DemoAsset.Health/value') and c['before']==42 and c['after']==99 for c in delta['changes'])
    checks+=1
    assert properties['/Script/Test.DemoAsset.Title']['value']=='A\U0001f600'
    assert properties['/Script/Test.DemoAsset.Mode']['serializedZero'] is True
    checks+=2
    alias=root/'alias.usmap';os.link(source,alias)
    try:
        try:build_cooked_mappings(metadata,alias)
        except ValueError:pass
        else:raise AssertionError('Input hard link was overwritten')
        assert json.loads(source.read_text(encoding='utf-8'))==package
        checks+=1
    finally:alias.unlink()
    dimensions=root/'dimensions.json'
    binding={'format':'cyuediff.uht-dimensions/1','metadataSha256':report['metadataSha256'],
             'fields':{'/Script/Test.Unresolved.Array':{'expression':'UNKNOWN_COUNT','size':2}}}
    dimensions.write_text(json.dumps(binding),encoding='utf-8')
    resolved=root/'resolved.usmap'
    resolved_report=build_cooked_mappings(metadata,resolved,static_dimensions=dimensions)
    assert resolved_report['schemas']==5 and resolved_report['staticDimensions']['fields']==binding['fields']
    loaded=run('usmap',resolved)
    definitions={v['path']:v for v in loaded['schemas']}
    assert definitions['/Script/Test.Unresolved']['slots']==2 and definitions['/Script/Test.Derived']['super']=='/Script/Test.Unresolved'
    checks+=2
    resolved_hash=hashlib.sha256(resolved.read_bytes()).hexdigest()
    cases=[]
    bad=copy.deepcopy(binding);bad['metadataSha256']='0'*64;cases.append(bad)
    bad=copy.deepcopy(binding);bad['fields']['/Script/Test.Unresolved.Array']['expression']='DIFFERENT';cases.append(bad)
    bad=copy.deepcopy(binding);bad['fields']={'/Script/Test.Missing.Array':{'expression':'UNKNOWN_COUNT','size':2}};cases.append(bad)
    for value in (0,256,True,1.5):
        bad=copy.deepcopy(binding);bad['fields']['/Script/Test.Unresolved.Array']['size']=value;cases.append(bad)
    bad=copy.deepcopy(binding);bad['fields']={'/Script/Test.DemoAsset.Samples':{'expression':'2','size':3}};cases.append(bad)
    for bad in cases:
        dimensions.write_text(json.dumps(bad),encoding='utf-8')
        try:build_cooked_mappings(metadata,resolved,static_dimensions=dimensions)
        except ValueError:pass
        else:raise AssertionError('Invalid static dimension evidence accepted')
        assert hashlib.sha256(resolved.read_bytes()).hexdigest()==resolved_hash
        checks+=1
    dimensions.write_text(json.dumps(binding),encoding='utf-8')
    protected=root/'dimension-alias.usmap';os.link(dimensions,protected)
    try:
        try:build_cooked_mappings(metadata,protected,static_dimensions=dimensions)
        except ValueError:pass
        else:raise AssertionError('Static dimension input hard link overwritten')
        assert json.loads(dimensions.read_text(encoding='utf-8'))==binding;checks+=1
    finally:protected.unlink()
    # UHT's LWC pseudo-property creates an FDoubleProperty in the matching engine.
    lwc=copy.deepcopy(package);lwc['Children'][1]['Children'][-1]=property('Mode','LargeWorldCoordinatesReal')
    source.write_text(json.dumps(lwc),encoding='utf-8')
    lwc_map=root/'lwc.usmap';lwc_report=build_cooked_mappings(metadata,lwc_map)
    saved_context=context.read_text(encoding='utf-8');lwc_context=json.loads(saved_context)
    lwc_context.update(usmap=lwc_map.name,sha256=lwc_report['sha256'])
    context.write_text(json.dumps(lwc_context),encoding='utf-8')
    lwc_value=run('zen-inspect',root/'schema.utoc','000000000000000000000001',*options)['exports'][0]['propertyRead']['properties']['/Script/Test.DemoAsset.Mode']
    assert lwc_value['type']=='DoubleProperty' and lwc_value['value']==0.0 and lwc_value['serializedZero']
    checks+=1
    source.write_text(json.dumps(package),encoding='utf-8');context.write_text(saved_context,encoding='utf-8')
    cyclic=copy.deepcopy(package)
    cyclic['Children'][0]['Super']='UDemoAsset'
    source.write_text(json.dumps(cyclic),encoding='utf-8')
    try:build_cooked_mappings(metadata,mapping)
    except ValueError:pass
    else:raise AssertionError('Cyclic native bases accepted')
    assert mapping.read_bytes()==first;checks+=1
    # Missing nested type evidence keeps the ordinal unavailable; it never shifts Health.
    damaged=copy.deepcopy(package)
    damaged['Children'][1]['Children'][2].pop('ValueProperty')
    source.write_text(json.dumps(damaged),encoding='utf-8')
    bad_report=build_cooked_mappings(metadata,root/'sparse.usmap')
    assert any(v['schema']=='/Script/Test.DemoAsset' and v['index']==1 for v in bad_report['unsupportedDescriptors'])
    checks+=1
    context_data=json.loads(context.read_text(encoding='utf-8'))
    context_data.update(usmap='sparse.usmap',sha256=hashlib.sha256((root/'sparse.usmap').read_bytes()).hexdigest())
    context.write_text(json.dumps(context_data),encoding='utf-8')
    unavailable=run('zen-inspect',root/'schema.utoc','000000000000000000000001',*options)
    assert unavailable['propertyExportsFailed']==1 and unavailable['propertyExportsDecoded']==1 and unavailable['exports'][0]['propertyRead']['status']=='opaque', unavailable['exports'][0]['propertyRead']
    checks+=1
    for invalid in [dict(package,schema='unknown'),dict(package,Children=package['Children']+[package['Children'][0]])]:
        source.write_text(json.dumps(invalid),encoding='utf-8')
        try:build_cooked_mappings(metadata,mapping)
        except ValueError:pass
        else:raise AssertionError('Invalid metadata accepted')
        assert mapping.read_bytes()==first;checks+=1
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==value for p,value in originals.items())
    checks+=1
print(f'{checks} UHT mapping/cooked value checks passed; original inputs unchanged')
