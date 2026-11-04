"""Validate native Blueprint diffs on workspace copies of a real loose asset.

Requires a monolithic editor Blueprint with a connection and a nonempty ASCII
pin default. Originals are only read; no Unreal asset writer is implemented.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset',type=Path,required=True)
parser.add_argument('--exe',type=Path,default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir',type=Path,default=Path('build-native/blueprint-validation'))
args=parser.parse_args()
original=args.asset.resolve(strict=True)
exe=args.exe.resolve(strict=True)
out=args.output_dir.resolve()
out.mkdir(parents=True,exist_ok=True)

def run(*arguments,expected=0):
    result=subprocess.run([str(exe),*map(str,arguments)],capture_output=True,text=True,encoding='utf-8',check=False)
    if result.returncode!=expected:
        raise RuntimeError(f'Unexpected exit {result.returncode}: {result.stderr}')
    return json.loads(result.stdout)

def wire_guid(value):
    return b''.join(struct.pack('<I',int(value[i:i+8],16)) for i in range(0,32,8))

def save_copy(name,data):
    target=out/name
    if target.resolve()==original or (target.exists() and target.samefile(original)):
        raise RuntimeError('Test output would overwrite its source')
    target.write_bytes(data)
    return target

report=run('inspect',original)
raw=original.read_bytes()
checksum=hashlib.sha256(raw).hexdigest()
assert checksum==report['fingerprints']['package']['sha256']
assert report['completeInput']
for entry in report['coverage'].values():
    if entry['offset']+entry['bytes']>len(raw):
        parser.error('This validation requires a monolithic Blueprint, without .uexp export data.')

# Change exactly one byte of a decoded default, leaving all serialized sizes intact.
selected=None
for graph_id,graph in report['blueprint']['graphs'].items():
    for node_id,node in graph['nodes'].items():
        for pin_id,pin in node['pins'].items():
            value=pin.get('defaultValue','')
            if value and value.isascii() and value[0].isdigit():
                selected=(graph_id,node_id,node,pin_id,value)
                break
        if selected:break
    if selected:break
if not selected:parser.error('Select a Blueprint with a nonempty numeric pin default.')
graph_id,node_id,node,pin_id,value=selected
entry=report['coverage'][node['path']]
start=entry['offset']+entry['nativeStart']
end=entry['offset']+entry['nativeEnd']
first=raw.index(wire_guid(pin_id),start,end)
body=raw.index(wire_guid(pin_id),first+16,end)
pattern=struct.pack('<i',len(value)+1)+value.encode('ascii')+b'\0'
field=raw.index(pattern,body+16,end)
changed=bytearray(raw)
new_value=('1' if value[0]!='1' else '2')+value[1:]
changed[field+4]=ord(new_value[0])
copy=save_copy('default-changed.uasset',changed)
diff=run('diff',original,copy,expected=1)
expected=f'/graphs/{graph_id}/nodes/{node_id}/pins/{pin_id}/defaultValue'
assert len(diff['graphChanges'])==1,diff['graphChanges']
assert diff['graphChanges'][0]=={'op':'replace','path':expected,'before':value,'after':new_value}
(out/'default-diff.json').write_text(json.dumps(diff,indent=2),encoding='utf-8')

# A nonzero unknown peer GUID keeps serialization valid while making a dangling link.
selected=None
for graph_id,graph in report['blueprint']['graphs'].items():
    for edge in graph['links'].values():
        if edge['resolved'] and edge['reciprocal']:
            selected=(graph,edge);break
    if selected:break
if not selected:parser.error('Select a Blueprint containing a reciprocal connection.')
graph,edge=selected
source=graph['nodes'][edge['from']['node']]
target=graph['nodes'][edge['to']['node']]
entry=report['coverage'][source['path']]
peer_index=report['coverage'][target['path']]['exportIndex']
pattern=struct.pack('<ii',0,peer_index)+wire_guid(edge['to']['pin'])
start=entry['offset']+entry['nativeStart'];end=entry['offset']+entry['nativeEnd']
reference=raw.index(pattern,start,end)
assert raw.find(pattern,reference+len(pattern),end)==-1
changed=bytearray(raw)
changed[reference+8:reference+24]=wire_guid('0123456789abcdef0123456789abcdef')
copy=save_copy('dangling-link.uasset',changed)
dangling=run('inspect',copy)
assert any(not g['complete'] for g in dangling['blueprint']['graphs'].values())
assert any(not link['resolved'] for g in dangling['blueprint']['graphs'].values() for link in g['links'].values())
assert not dangling['diagnostics'],dangling['diagnostics']
(out/'dangling-inspection.json').write_text(json.dumps(dangling,indent=2),encoding='utf-8')
# Unknown serialization profiles must preserve the opaque payload instead of
# claiming that the decoded graph has no pins.
changed=bytearray(raw)
framework=wire_guid('cffc743f43b04480939114df171d2073')
version_offset=raw.index(framework,0,report['summary']['nameOffset'])+16
assert struct.unpack_from('<i',raw,version_offset)[0]==37
struct.pack_into('<i',changed,version_offset,38)
copy=save_copy('unsupported-profile.uasset',changed)
unsupported=run('inspect',copy)
assert any('pin-profile' in diagnostic for diagnostic in unsupported['diagnostics'])
assert all(not graph['complete'] for graph in unsupported['blueprint']['graphs'].values())
assert any(entry['nativeCoverage']=='opaque' for entry in unsupported['coverage'].values())
assert hashlib.sha256(original.read_bytes()).hexdigest()==checksum
summary={'defaultChange':diff['graphChanges'][0],
         'graphs':len(report['blueprint']['graphs']),
         'nodes':sum(len(g['nodes']) for g in report['blueprint']['graphs'].values()),
         'pins':sum(len(n['pins']) for g in report['blueprint']['graphs'].values() for n in g['nodes'].values()),
         'links':sum(len(g['links']) for g in report['blueprint']['graphs'].values()),
         'danglingLinkRetained':True,'unknownProfilePreservedAsOpaque':True,'originalUnchanged':True}
(out/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')
print(json.dumps(summary,indent=2))
