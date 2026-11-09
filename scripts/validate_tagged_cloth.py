"""Validate selected tagged source cloth values against an actual UE 5.3 API reference.

This validates stored fields and certified native tails, without resolving class defaults,
executing a simulation or projecting source render/physical correspondence.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import subprocess


def need(ok, message):
    if not ok:
        raise ValueError(message)


def compare(actual, reference, mesh, context_sha256, read_limits=None):
    need(reference.get('schema') == 'cyuediff.unreal-physical-cloth-geometry-reference/1' and reference.get('engine', '').startswith('5.3.2-'), 'Select an actual UE 5.3.2 API reference')
    for key,value in [('ue4Version',522),('ue5Version',1009),('licenseeVersion',0)]:
        need(type(actual.get('summary',{}).get(key)) is int and actual['summary'][key] == value, 'Source archive profile differs')
    context=actual.get('sourceTypeContext',{})
    need(context.get('sha256') == context_sha256 and context.get('profileMatched') is True and context.get('defaultsResolved') is False, 'Explicit type context provenance differs')
    need(actual.get('sourceReadLimits') == read_limits, 'Source read limit provenance differs')
    if read_limits is not None:
        need(type(actual['sourceReadLimits']) is dict and all(type(actual['sourceReadLimits'].get(k)) is int for k in ['maxProperties','maxPropertyBytes']), 'Source policy requires exact integer values')
    need(actual.get('sourceReadLimitFailed',False) is False, 'A source limit/count guard failed')
    objects = actual['snapshot']['objects']
    need(objects[mesh]['class'] == '/Script/Engine.SkeletalMesh', 'Selected source mesh identity differs')
    ref_mesh = reference['object'].replace(':', '.', 1)
    need(ref_mesh.rsplit('.', 1)[-1] == mesh and '/' not in mesh, 'Declare the exact loose source export paired with the reference')
    def paired(path):
        path = path.replace(':', '.', 1)
        need(path.startswith(ref_mesh + '.'), 'Reference child is outside the selected mesh')
        return mesh + path[len(ref_mesh):]
    inventory = {k for k,v in objects.items() if v['class'] == '/Script/ClothingSystemRuntimeCommon.ClothingAssetCommon'}
    need(inventory == {paired(x['object']) for x in reference['assets']} and inventory, 'Physical asset inventory differs')
    stats = {'values': 0, 'lods': 0, 'tethers': 0, 'rawTransitionRecords': 0, 'nativeBytes': 0, 'unavailableDefaultFields': []}
    def same(a, b, kind=None):
        if isinstance(b, list):
            need(isinstance(a, list) and len(a) == len(b), 'Stored array inventory differs')
            for x,y in zip(a,b): same(x,y,kind)
        elif kind == 'float':
            need(type(a) in (int,float) and type(b) in (int,float) and math.isfinite(a) and math.isfinite(b), 'Expected a finite stored float')
            try: packed = struct.pack('<f', a); expected = struct.pack('<f', b)
            except (OverflowError, struct.error) as e: raise ValueError('Float32 range differs') from e
            need(struct.unpack('<f',packed)[0] == a and ((a == 0 and b == 0) or packed == expected), 'Exact stored float32 differs')
            stats['values'] += 1
        else:
            need(type(a) is type(b) and a == b, 'Stored scalar/type differs')
            stats['values'] += 1
    def field(props, name, kind):
        entry = props[name]
        need(entry['type']['name'] == kind and 'value' in entry and 'opaque' not in entry, 'Missing typed stored field ' + name)
        return entry['value']
    def hybrid(value, name):
        need(value.get('prefix') == 'tagged-properties' and value.get('decoder') == 'selected.source.' + name + '/1', 'Unexpected source serializer contract')
        need(set(value['data']) == {'properties','native'}, 'Incomplete source hybrid')
        return value['data']['properties'], value['data']['native']
    def native_bytes(value):
        need(isinstance(value,list) and all(type(x) is int and 0<=x<=255 for x in value), 'Expected exact reference native bytes')
        return bytes(value)
    def tethers(value, ref):
        props,native = hybrid(value, 'ClothTetherData')
        need(not props and set(native) == {'batches'}, 'Tether prefix/tail differs')
        batches = native['batches']; need(len(batches) == len(ref['batches']), 'Tether batch count differs')
        encoded = bytearray(struct.pack('<i',len(batches)))
        for batch, expected in zip(batches, ref['batches']):
            need(len(batch) == len(expected), 'Tether count differs'); encoded.extend(struct.pack('<i',len(batch)))
            for item, target in zip(batch,expected):
                need(set(item) == {'anchor','particle','length'}, 'Tether field inventory differs')
                same(item['anchor'],target[0]); same(item['particle'],target[1]); same(item['length'],target[2],'float')
                encoded.extend(struct.pack('<iif',item['anchor'],item['particle'],item['length'])); stats['tethers'] += 1
        need(encoded == native_bytes(ref['nativeBytes']), 'Exact native tether serialization differs'); stats['nativeBytes'] += len(encoded)
    for asset in reference['assets']:
        props = objects[paired(asset['object'])]['properties']
        same(field(props,'AssetGuid','StructProperty'), ''.join(format(x,'08x') for x in asset['assetGuidWords']))
        for name,target in [('LodMap','lodMap'),('UsedBoneIndices','usedBoneIndices'),('UsedBoneNames','usedBoneNames')]: same(field(props,name,'ArrayProperty'),asset[target])
        if 'ReferenceBoneIndex' in props: same(field(props,'ReferenceBoneIndex','IntProperty'),asset['referenceBoneIndex'])
        else: stats['unavailableDefaultFields'].append(paired(asset['object']) + '.ReferenceBoneIndex')
        lods = field(props,'LodData','ArrayProperty'); need(len(lods) == len(asset['lods']), 'LOD inventory differs')
        for l,ref in zip(lods,asset['lods']):
            prefix,native = hybrid(l,'ClothLODDataCommon'); stats['lods'] += 1
            physical = field(prefix,'PhysicalMeshData','StructProperty')
            for name,target in [('Vertices','vertices'),('Normals','normals'),('Indices','indices'),('InverseMasses','inverseMasses'),('SelfCollisionIndices','selfCollisionIndices')]:
                same(field(physical,name,'ArrayProperty'),ref[target],'float' if name in ['Vertices','Normals','InverseMasses'] else None)
            for name,target in [('MaxBoneWeights','maxBoneWeights'),('NumFixedVerts','fixedVertices')]: same(field(physical,name,'IntProperty'),ref[target])
            bones = field(physical,'BoneData','ArrayProperty'); need(len(bones) == len(ref['boneData']), 'Bone inventory differs')
            for bone,target in zip(bones,ref['boneData']):
                same(field(bone,'NumInfluences','IntProperty'),target['influences'])
                need(len(target['indices']) == 12 and len(target['weights']) == 12, 'Expected twelve static bone slots')
                for i in range(12):
                    suffix = '['+str(i)+']' if i else ''
                    same(field(bone,'BoneIndices'+suffix,'UInt16Property'),target['indices'][i])
                    same(field(bone,'BoneWeights'+suffix,'FloatProperty'),target['weights'][i],'float')
            maps = field(physical,'WeightMaps','MapProperty')
            need(not maps['removed'] and len(maps['entries']) == len(ref['weightMaps']), 'Stored weight map inventory differs')
            for target in ref['weightMaps']:
                entry = maps['entries'][str(target['target'])]; same(entry['key'],target['target'])
                same(field(entry['value'],'Values','ArrayProperty'),target['values'],'float')
            for name,target in [('EuclideanTethers','euclideanTethers'),('GeodesicTethers','geodesicTethers')]: tethers(field(physical,name,'StructProperty'),ref[target])
            need(set(native) == {'transitionUp','transitionDown'}, 'Transition tail inventory differs')
            for name,target in [('transitionUp','transitionUpNativeBytes'),('transitionDown','transitionDownNativeBytes')]:
                records = native[name]; need(all(isinstance(x,str) and re.fullmatch('[0-9a-f]{128}',x) for x in records), 'Invalid raw transition record')
                encoded = struct.pack('<i',len(records)) + b''.join(bytes.fromhex(x) for x in records)
                need(encoded == native_bytes(ref[target]), 'Exact native transition serialization differs')
                stats['rawTransitionRecords'] += len(records); stats['nativeBytes'] += len(encoded)
            for name,target,kind in [('SkinningKernelRadius','skinningKernelRadius','FloatProperty'),('bUseMultipleInfluences','multipleInfluences','BoolProperty'),('bSmoothTransition','smoothTransition','BoolProperty')]:
                same(field(prefix,name,kind),ref[target],'float' if kind == 'FloatProperty' else None)
        links = field(props,'ClothConfigs','MapProperty'); need(not links['removed'] and len(links['entries']) == len(asset['configs']), 'Stored config inventory differs')
        for config in asset['configs']:
            key = config['class'].rsplit('.',1)[-1]; link = links['entries'][json.dumps(key)]
            same(link['key'],key); same(link['value'],paired(config['object']))
            obj = objects[paired(config['object'])]; need(obj['class'] == config['class'], 'Config class differs')
            for name,target in config.items():
                if name in ('object','class'): continue
                parts = name.split('.')
                if parts[0] not in obj['properties']:
                    stats['unavailableDefaultFields'].append(paired(config['object'])+'.'+name); continue
                value = field(obj['properties'],parts[0],'StructProperty' if len(parts)>1 else 'IntProperty' if name=='IterationCount' else 'FloatProperty')
                if len(parts)>1: value = field(value,parts[1],'FloatProperty')
                same(value,target,None if name=='IterationCount' else 'float')
    return stats


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ['manifest','consumer','layouts','mapping','output-dir']: parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--max-properties',type=int)
    parser.add_argument('--max-property-bytes',type=int)
    args=parser.parse_args(); manifest=args.manifest.resolve(strict=True); catalog=args.layouts.resolve(strict=True); consumer=args.consumer.resolve(strict=True)
    document=json.loads(manifest.read_text(encoding='utf-8')); need(document.get('schema') == 'cyuediff.tagged-cloth-validation/1','Unexpected source validation manifest')
    cases=document['cases']; need(0<len(cases)<=1000,'Select explicit source/reference pairs')
    mapping=args.mapping.resolve(strict=True)
    inputs={manifest,catalog,consumer,mapping,Path(__file__).resolve()}
    limits={'maxProperties':100000,'maxPropertyBytes':33554432}; consumer_options=[]
    for flag,key,maximum in [('max-properties','maxProperties',4000000),('max-property-bytes','maxPropertyBytes',268435456)]:
        value=getattr(args,flag.replace('-','_'))
        if value is not None:
            need(1<=value<=maximum,'Source read limit outside its finite range');limits[key]=value;consumer_options.extend(['--'+flag,str(value)])
    expected_limits=None if limits=={'maxProperties':100000,'maxPropertyBytes':33554432} else limits
    pairs=[]
    for case in cases:
        asset=(manifest.parent/case['asset']).resolve(strict=True); ref=(manifest.parent/case['reference']).resolve(strict=True)
        inputs.update((asset,ref))
        for ext in ['.uexp','.ubulk','.uptnl','.m.ubulk','.o.ubulk','.upayload']:
            side=asset.with_suffix(ext)
            if side.exists(): inputs.add(side.resolve())
        pairs.append((asset,ref,case['mesh']))
    output=args.output_dir.resolve(); need(not any(output==p or output in p.parents for p in inputs),'Output contains protected inputs'); output.mkdir(parents=True,exist_ok=False)
    digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest(); hashes={str(p):digest(p) for p in inputs}
    context=json.loads(catalog.read_text(encoding='utf-8'))['taggedTypes']; identity=hashlib.sha256(json.dumps(context,sort_keys=True,separators=(',',':'),ensure_ascii=False).encode('utf-8')).hexdigest(); results=[]
    need(context.get('usmapSha256') == digest(mapping),'Mapping fingerprint differs')
    try:
        for i,(asset,ref,mesh) in enumerate(pairs):
            result=subprocess.run([str(consumer),'package',str(catalog),str(asset),*consumer_options],capture_output=True,timeout=120)
            need(result.returncode==0,result.stderr.decode('utf-8','replace') or 'Source consumer failed')
            projection=json.loads(result.stdout)['before']; stats=compare(projection,json.loads(ref.read_text(encoding='utf-8')),mesh,identity,expected_limits); results.append(stats)
            (output/('%03d.json'%i)).write_text(json.dumps(projection,indent=2)+'\n',encoding='utf-8',newline='\n')
    finally:
        need(all(digest(Path(p))==h for p,h in hashes.items()),'Protected inputs changed')
    summary={'schema':'cyuediff.tagged-cloth-validation/1','cases':results,'inputSha256':hashes,'inputsUnchanged':True,'defaultsResolved':False}
    for name in ['values','lods','tethers','rawTransitionRecords','nativeBytes']: summary[name]=sum(x[name] for x in results)
    (output/'summary.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8',newline='\n'); print(json.dumps({k:summary[k] for k in ['values','lods','tethers','nativeBytes','inputsUnchanged']}))


if __name__=='__main__': main()
