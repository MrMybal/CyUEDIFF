"""Validate material reading/diffs on workspace copies of loose UE5 editor assets.

Controlled byte edits preserve lengths. This is a reader test, not an asset writer.
Source assets are only read; no shader compilation or parent evaluation is performed.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--asset', type=Path, required=True, help='Material/function with a native input connection')
parser.add_argument('--instance', type=Path, required=True, help='Material instance with a serialized scalar override')
parser.add_argument('--exe', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/material-validation'))
args = parser.parse_args()
exe = args.exe.resolve(strict=True)
originals = [args.asset.resolve(strict=True), args.instance.resolve(strict=True)]
out = args.output_dir.resolve()
out.mkdir(parents=True, exist_ok=True)

def run(*arguments, expected=0):
    r = subprocess.run([str(exe), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
    if r.returncode != expected:
        raise RuntimeError(f'Unexpected exit {r.returncode}: {r.stderr}')
    return json.loads(r.stdout)

def save(name, data):
    path = out / name
    for original in originals:
        if path == original or (path.exists() and path.samefile(original)):
            raise RuntimeError('Test output would overwrite a source asset')
    path.write_bytes(data)
    return path

def wire_guid(value):
    return b''.join(struct.pack('<I', int(value[i:i+8], 16)) for i in range(0, 32, 8))

def pointer(value):
    return value.replace('~', '~0').replace('/', '~1')

def read_names(raw, summary):
    at = summary['nameOffset']
    names = []
    for _ in range(summary['nameCount']):
        length = struct.unpack_from('<i', raw, at)[0]
        at += 4
        size = abs(length) * (2 if length < 0 else 1)
        names.append(raw[at:at+size].decode('utf-16-le' if length < 0 else 'utf-8').rstrip('\0'))
        at += size + 4
    return {name: index for index, name in enumerate(names)}

def fname(names, name):
    return struct.pack('<ii', names[name], 0)

def inspect(original):
    report = run('inspect', original)
    raw = original.read_bytes()
    assert report['completeInput']
    if not 1000 <= report['summary']['ue5Version'] < 1011:
        parser.error('Validation byte edits currently require legacy UE5 property tags (before 1011).')
    if any(e['offset'] + e['bytes'] > len(raw) for e in report['coverage'].values()):
        parser.error('Validation byte edits currently require a monolithic asset.')
    return report, raw, read_names(raw, report['summary'])

report, raw, names = inspect(originals[0])
checksums = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}
selected = None
for graph_id, graph in report['materials']['graphs'].items():
    if not graph['complete']:
        continue
    for node_id, node in graph['nodes'].items():
        for port_id, port in node['inputs'].items():
            if '/' in port_id or port['type'] != 'ExpressionInput' or not port['decoded'] or not port.get('expression'):
                continue
            old = port['expression']
            candidates = [key for key in graph['nodes'] if key not in (old, node_id, '$result') and
                          (not graph['nodes'][key]['outputSchemaKnown'] or '0' in graph['nodes'][key]['outputs'])]
            if candidates:
                selected = graph_id, graph, node_id, node, port_id, port, candidates[0]
                break
        if selected: break
    if selected: break
if not selected:
    parser.error('Choose a material/function with a direct native input and another expression to connect.')
graph_id, graph, node_id, node, port_id, port, replacement = selected
entry = report['coverage'][node['path']]
start, end = entry['offset'], entry['offset'] + entry['bytes']
pattern = fname(names, port_id) + fname(names, 'StructProperty') + struct.pack('<ii', 36, 0) + fname(names, 'ExpressionInput') + bytes(16) + b'\0'
at = raw.index(pattern, start, end) + len(pattern)
assert raw.find(pattern, at, end) == -1
old_index = report['coverage'][graph['nodes'][port['expression']]['path']]['exportIndex']
assert struct.unpack_from('<i', raw, at)[0] == old_index
changed = bytearray(raw)
new_index = report['coverage'][graph['nodes'][replacement]['path']]['exportIndex']
struct.pack_into('<ii', changed, at, new_index, 0)
copy = save('rewired.uasset', changed)
diff = run('material-diff', originals[0], copy, expected=1)
expression_path = f'/graphs/{pointer(graph_id)}/nodes/{pointer(node_id)}/inputs/{pointer(port_id)}/expression'
assert any(c['path'] == expression_path and c['before'] == port['expression'] and c['after'] == replacement for c in diff['changes'])
full_diff = run('diff', originals[0], copy, expected=1)
assert full_diff['materialChanges'] == diff['changes']
(out / 'rewired-diff.json').write_text(json.dumps(diff, indent=2), encoding='utf-8')

# A valid package reference to the root cannot be resolved to an expression.
changed = bytearray(raw)
root_index = report['coverage'][graph_id]['exportIndex']
struct.pack_into('<i', changed, at, root_index)
copy = save('dangling.uasset', changed)
dangling = run('material', copy)['graphs'][graph_id]
assert not dangling['complete'] and any(not edge['resolved'] for edge in dangling['links'].values())
assert run('material-diff', copy, copy, expected=2)['status'] == 'incomplete'

# Unknown profiles must preserve native inputs as opaque and refuse equality.
changed = bytearray(raw)
version_at = raw.index(wire_guid('cffc743f43b04480939114df171d2073'), 0, report['summary']['nameOffset']) + 16
assert struct.unpack_from('<i', raw, version_at)[0] == 37
struct.pack_into('<i', changed, version_at, 38)
copy = save('unsupported-profile.uasset', changed)
future = run('inspect', copy)
assert not future['materials']['graphs'][graph_id]['complete']
assert run('material-diff', copy, copy, expected=2)['status'] == 'incomplete'

# Change one scalar override, identified by its decoded identity and unique value.
mi_report, mi_raw, mi_names = inspect(originals[1])
selected = None
for instance_id, instance in mi_report['materials']['instances'].items():
    scalars = instance['parameters'].get('scalar', {})
    counts = Counter(p['value'] for p in scalars.values() if p['valueDecoded'])
    for key, parameter in scalars.items():
        if not parameter['valueDecoded'] or counts[parameter['value']] != 1:
            continue
        pattern = fname(mi_names, 'ParameterValue') + fname(mi_names, 'FloatProperty') + struct.pack('<ii', 4, 0) + b'\0' + struct.pack('<f', parameter['value'])
        entry = mi_report['coverage'][instance_id]
        start, end = entry['offset'], entry['offset'] + entry['bytes']
        location = mi_raw.find(pattern, start, end)
        if location >= 0 and mi_raw.find(pattern, location + len(pattern), end) == -1:
            selected = instance_id, key, parameter, location + len(pattern) - 4
            break
    if selected: break
if not selected:
    parser.error('Choose an instance with a unique explicitly serialized scalar value.')
instance_id, key, parameter, location = selected
changed = bytearray(mi_raw)
new_value = parameter['value'] + 1.0
struct.pack_into('<f', changed, location, new_value)
copy = save('parameter-changed.uasset', changed)
parameter_diff = run('material-diff', originals[1], copy, expected=1)
value_path = f'/instances/{pointer(instance_id)}/parameters/scalar/{pointer(key)}/value'
assert parameter_diff['changes'] == [{'op': 'replace', 'path': value_path, 'before': parameter['value'], 'after': new_value}]
(out / 'parameter-diff.json').write_text(json.dumps(parameter_diff, indent=2), encoding='utf-8')
# Exercise command-specific refusals and the common output guard on a disposable copy.
guard = save('output-guard.uasset', raw)
for arguments in [('material', guard, '--out', guard), ('material', guard, '--metadata'),
                  ('material-diff', guard, guard, '--metadata')]:
    r = subprocess.run([str(exe), *map(str, arguments)], capture_output=True, text=True, encoding='utf-8')
    assert r.returncode == 2 and 'error' in json.loads(r.stderr)
assert guard.read_bytes() == raw
for original in originals:
    assert hashlib.sha256(original.read_bytes()).hexdigest() == checksums[str(original)]
summary = {'graph': graph_id, 'nodes': len(graph['nodes']), 'links': len(graph['links']),
           'rewireDetected': True, 'danglingLinkRetained': True, 'unknownProfileOpaque': True,
           'incompleteEqualityRefused': True, 'scalarChange': parameter_diff['changes'][0], 'originalsUnchanged': True, 'cliRefusals': 3}
(out / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
print(json.dumps(summary, indent=2))
