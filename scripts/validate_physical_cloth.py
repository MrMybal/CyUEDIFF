"""Compare explicitly mapped cooked cloth properties/tails with a matching Engine API reference."""
import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import struct
import subprocess

ROOT_CLASS='/Script/ClothingSystemRuntimeCommon.ClothingAssetCommon'
LOD='/Script/ClothingSystemRuntimeCommon.ClothLODDataCommon'
PHYSICAL='/Script/ClothingSystemRuntimeCommon.ClothPhysicalMeshData'
BONE='/Script/ClothingSystemRuntimeInterface.ClothVertBoneData'
POINT='/Script/ClothingSystemRuntimeCommon.PointWeightMap'


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def field(properties, schema, name):
    value=properties[schema+'.'+name]
    if not isinstance(value,dict) or 'value' not in value:
        raise ValueError('Missing typed stored field '+schema+'.'+name)
    arrays={'LodData','LodMap','UsedBoneIndices','UsedBoneNames','Vertices','Normals','Indices','InverseMasses','SelfCollisionIndices','BoneData','Values'}
    floats={'SkinningKernelRadius','Low','High','DampingCoefficient','GravityScale'}
    integers={'ReferenceBoneIndex','NumInfluences','MaxBoneWeights','NumFixedVerts','IterationCount'}
    structs={'AssetGuid','PhysicalMeshData','EuclideanTethers','GeodesicTethers','EdgeStiffnessWeighted'}
    expected='ArrayProperty' if name in arrays else 'FloatProperty' if name in floats or name.startswith('BoneWeights[') else 'IntProperty' if name in integers else 'UInt16Property' if name.startswith('BoneIndices[') else 'StructProperty' if name in structs else 'MapProperty' if name in ['WeightMaps','ClothConfigs'] else 'BoolProperty' if name in ['bUseMultipleInfluences','bSmoothTransition'] else None
    if expected and value.get('type')!=expected:raise ValueError('Stored field type differs '+schema+'.'+name)
    return value['value']


def compare(actual, reference):
    if reference.get('schema')!='cyuediff.unreal-physical-cloth-reference/1' or not reference.get('engine','').startswith('5.3.'):
        raise ValueError('Select the explicit UE 5.3 physical cloth reference')
    if actual.get('profile')!='ue5.3' or actual.get('propertyExportsFailed'):
        raise ValueError('Unverified or failed cooked property projection')
    exports={row['path']:row for row in actual['exports']}
    if len(exports)!=len(actual['exports']):raise ValueError('Duplicate actual object identity')
    def canonical_path(path):
        # Engine GetPathName uses ':' at the asset/subobject boundary; Zen outer paths use '.'.
        if not isinstance(path,str) or not path.startswith('/') or '\0' in path:raise ValueError('Invalid reference object identity')
        return path.replace(':','.',1)
    owner=canonical_path(reference['object'])
    if owner not in exports or exports[owner]['class']!='/Script/Engine.SkeletalMesh':raise ValueError('Reference mesh identity differs')
    assets=reference['assets']
    if not assets or len({row['object'] for row in assets})!=len(assets):raise ValueError('Missing or duplicate reference cloth assets')
    actual_assets={row['path'] for row in actual['exports'] if row['class']==ROOT_CLASS}
    if actual_assets!={canonical_path(row['object']) for row in assets}:raise ValueError('Physical asset inventory differs')
    values=lods=vertices=triangles=tethers=transition_records=config_values=0
    unavailable=[]

    def same(a,b,kind=None):
        nonlocal values
        if isinstance(b,list):
            if not isinstance(a,list) or len(a)!=len(b):raise ValueError('Stored array size differs')
            for x,y in zip(a,b):same(x,y,kind)
            return
        if kind=='bool' or isinstance(b,bool):
            if type(a) is not bool or type(b) is not bool or a!=b:raise ValueError('Stored boolean differs')
        elif kind=='int':
            if type(a) is not int or type(b) is not int or a!=b:raise ValueError('Stored integer differs')
        elif kind=='float':
            if isinstance(a,bool) or isinstance(b,bool) or not isinstance(a,(int,float)) or not isinstance(b,(int,float)) or not math.isfinite(a) or not math.isfinite(b):
                raise ValueError('Non-finite or invalid stored scalar')
            # Ordinary Engine JSON can lose the sign of zero. Exact native bytes are checked separately.
            if a!=0 or b!=0:
                if struct.pack('<f',a)!=struct.pack('<f',b):raise ValueError('Stored float32 differs')
        elif not isinstance(a,str) or not isinstance(b,str) or a!=b:raise ValueError('Stored string differs')
        values+=1

    def props(path, cls):
        row=exports[canonical_path(path)]
        if row['class']!=cls or row.get('propertyRead',{}).get('status')!='decoded':
            raise ValueError('Missing explicit decoded object '+path)
        if row['propertyRead'].get('defaultsResolved') is not False or row['propertyRead'].get('nativePayloadDecoded') is not False:
            raise ValueError('Unsupported default/native-export coverage claim')
        return row['propertyRead']['properties']

    def native_bytes(value):
        if not isinstance(value,list) or any(type(x) is not int or not 0<=x<=255 for x in value):
            raise ValueError('Invalid reference native bytes')
        return bytes(value)

    def check_tethers(value, source):
        nonlocal tethers
        if value.get('properties')!={}:raise ValueError('Unexpected tether reflected prefix')
        batches=value['native']['batches']
        if len(batches)!=len(source['batches']):raise ValueError('Tether batch inventory differs')
        encoded=bytearray(struct.pack('<i',len(batches)))
        for batch,ref in zip(batches,source['batches']):
            if len(batch)!=len(ref):raise ValueError('Tether batch size differs')
            encoded.extend(struct.pack('<i',len(batch)))
            for row,expected in zip(batch,ref):
                if set(row)!={'anchor','particle','length'}:raise ValueError('Tether fields differ')
                same(row['anchor'],expected[0],'int');same(row['particle'],expected[1],'int');same(row['length'],expected[2],'float')
                encoded.extend(struct.pack('<iif',row['anchor'],row['particle'],row['length']))
                tethers+=1
        if encoded!=native_bytes(source['nativeBytes']):raise ValueError('Exact native tether serialization differs')

    for source in assets:
        p=props(source['object'],ROOT_CLASS)
        guid=field(p,'/Script/ClothingSystemRuntimeInterface.ClothingAssetBase','AssetGuid')
        expected=''.join(format(x,'08x') for x in source['assetGuidWords'])
        same(guid,expected)
        for name,ref in [('LodMap','lodMap'),('UsedBoneIndices','usedBoneIndices'),('UsedBoneNames','usedBoneNames')]:
            same(field(p,ROOT_CLASS,name),source[ref],'string' if name=='UsedBoneNames' else 'int')
        if ROOT_CLASS+'.ReferenceBoneIndex' in p:
            same(field(p,ROOT_CLASS,'ReferenceBoneIndex'),source['referenceBoneIndex'],'int')
        else:unavailable.append(source['object']+'.ReferenceBoneIndex')
        stored_lods=field(p,ROOT_CLASS,'LodData')
        if len(stored_lods)!=len(source['lods']):raise ValueError('Physical LOD count differs')
        for l,ref in zip(stored_lods,source['lods']):
            lods+=1
            prefix=l['properties'];physical=field(prefix,LOD,'PhysicalMeshData')
            for name,target in [('Vertices','vertices'),('Normals','normals'),('Indices','indices'),('InverseMasses','inverseMasses'),('SelfCollisionIndices','selfCollisionIndices'),('MaxBoneWeights','maxBoneWeights'),('NumFixedVerts','fixedVertices')]:
                same(field(physical,PHYSICAL,name),ref[target],'float' if name in ['Vertices','Normals','InverseMasses'] else 'int')
            vertices+=len(ref['vertices']);triangles+=len(ref['indices'])//3
            bone_data=field(physical,PHYSICAL,'BoneData')
            if len(bone_data)!=len(ref['boneData']):raise ValueError('Physical bone record count differs')
            for row,b in zip(bone_data,ref['boneData']):
                same(field(row,BONE,'NumInfluences'),b['influences'],'int')
                if len(b['indices'])!=12 or len(b['weights'])!=12:raise ValueError('Expected explicit 12-slot bone records')
                for i in range(12):
                    same(field(row,BONE,'BoneIndices['+str(i)+']'),b['indices'][i],'int')
                    same(field(row,BONE,'BoneWeights['+str(i)+']'),b['weights'][i],'float')
            maps=field(physical,PHYSICAL,'WeightMaps')
            if maps['removed'] or len(maps['entries'])!=len(ref['weightMaps']):raise ValueError('Weight map inventory differs')
            for target in ref['weightMaps']:
                entry=maps['entries'][str(target['target'])]
                same(entry['key'],target['target'],'int')
                same(field(entry['value'],POINT,'Values'),target['values'],'float')
            check_tethers(field(physical,PHYSICAL,'EuclideanTethers'),ref['euclideanTethers'])
            check_tethers(field(physical,PHYSICAL,'GeodesicTethers'),ref['geodesicTethers'])
            for name,target in [('transitionUp','transitionUpNativeBytes'),('transitionDown','transitionDownNativeBytes')]:
                records=l['native'][name]
                if any(not isinstance(x,str) or not re.fullmatch('[0-9a-f]{128}',x) for x in records):raise ValueError('Expected exact raw 64-byte transition records')
                encoded=struct.pack('<i',len(records))+b''.join(bytes.fromhex(x) for x in records)
                if encoded!=native_bytes(ref[target]):raise ValueError('Exact native transition serialization differs')
                transition_records+=len(records)
            for name,target in [('SkinningKernelRadius','skinningKernelRadius'),('bUseMultipleInfluences','multipleInfluences'),('bSmoothTransition','smoothTransition')]:
                same(field(prefix,LOD,name),ref[target],'float' if name=='SkinningKernelRadius' else 'bool')
        links=field(p,ROOT_CLASS,'ClothConfigs')
        if links['removed'] or len(links['entries'])!=len(source['configs']):raise ValueError('Stored config inventory differs')
        for config in source['configs']:
            # These selected real references use the actual GetClothConfig<>() class-name keys.
            key_name=config['class'].rsplit('.',1)[1]
            link=links['entries'][json.dumps(key_name,separators=(',',':'))]
            if link['key']!=key_name or link['value']!={'kind':'export','path':canonical_path(config['object'])}:
                raise ValueError('Stored config identity differs')
            properties=props(config['object'],config['class'])
            for name,expected_value in config.items():
                if name in ('object','class'):continue
                parts=name.split('.')
                key=config['class']+'.'+parts[0]
                if key not in properties:
                    unavailable.append(config['object']+'.'+name);continue
                scalar=field(properties,config['class'],parts[0])
                if len(parts)==2:scalar=field(scalar,'/Script/ChaosCloth.ChaosClothWeightedValue',parts[1])
                same(scalar,expected_value,'int' if name=='IterationCount' else 'float');config_values+=1
    return {'values':values,'assets':len(assets),'lods':lods,'vertices':vertices,'triangles':triangles,
            'tethers':tethers,'rawTransitionRecords':transition_records,'configValues':config_values,
            'unavailableDefaultFields':unavailable}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['manifest','consumer','container','global-toc','schema','output-dir']:
        parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args()
    manifest=args.manifest.resolve(strict=True);cases=json.loads(manifest.read_text(encoding='utf-8'))['cases']
    if not cases or len(cases)>10000 or any(not re.fullmatch('[0-9a-fA-F]{24}',x['entry']) for x in cases):parser.error('Select 1..10000 explicit package chunk IDs')
    refs=[(manifest.parent/x['reference']).resolve(strict=True) for x in cases]
    context=json.loads(args.schema.read_text(encoding='utf-8'))
    mapping=(args.schema.parent/context['usmap']).resolve(strict=True)
    protected={Path(__file__).resolve(),manifest,args.consumer.resolve(strict=True),args.schema.resolve(strict=True),mapping,*refs}
    for p in [args.container,args.global_toc]:
        protected.add(p.resolve(strict=True));protected.update(x.resolve() for x in p.parent.glob(p.stem+'*.ucas'))
    output=args.output_dir.resolve()
    if any(output==p or output in p.parents for p in protected):parser.error('Output directory contains a protected input')
    output.mkdir(parents=True,exist_ok=False)
    hashes={str(p):digest(p) for p in protected};results=[]
    env={k.upper() if os.name=='nt' else k:v for k,v in os.environ.items()}
    try:
        for i,(case,reference) in enumerate(zip(cases,refs)):
            p=subprocess.run([str(args.consumer.resolve()),str(args.container.resolve()),case['entry'],str(args.global_toc.resolve()),str(args.schema.resolve())],capture_output=True,env=env,timeout=120)
            if p.returncode:raise ValueError(p.stderr.decode('utf-8','replace') or 'Failed bound property projection')
            actual=json.loads(p.stdout)['before'];result=compare(actual,json.loads(reference.read_text(encoding='utf-8')))
            result['entry']=case['entry'];results.append(result)
            (output/('%03d.json'%i)).write_text(json.dumps(actual,indent=2)+'\n',encoding='utf-8',newline='\n')
    finally:
        changed=[p for p,h in hashes.items() if digest(Path(p))!=h]
        if changed:raise ValueError('Protected inputs changed: '+', '.join(changed))
    summary={'schema':'cyuediff.physical-cloth-validation/1','cases':results,'inputSha256':hashes,'inputsUnchanged':True}
    for key in ['values','assets','lods','vertices','triangles','tethers','rawTransitionRecords','configValues']:
        summary[key]=sum(x[key] for x in results)
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(json.dumps({k:summary[k] for k in ['values','assets','lods','vertices','triangles','tethers','rawTransitionRecords','configValues','inputsUnchanged']}))


if __name__=='__main__':main()
