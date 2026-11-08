"""Compare stored UStruct declarations with an independent bounded Python wire reader.

Inputs are explicit host-owned loose package roots; reports and decoded private
metadata remain in ignored workspace build directories. No Unreal process runs.
"""
import argparse
from collections import Counter
import hashlib
import json
import shutil
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--input', type=Path, action='append', required=True)
parser.add_argument('--cli', type=Path, default=Path('build-core/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-goal/reflection-real'))
parser.add_argument('--max-files', type=int, default=200)
parser.add_argument('--controlled-diff', action='store_true', help='Change one stored declaration flag in a private copy')
args = parser.parse_args()
workspace = Path(__file__).resolve().parents[1]
out = args.output_dir.resolve()
if not out.is_relative_to(workspace) or not out.relative_to(workspace).parts[0].startswith('build'):
    parser.error('Reports must stay in an ignored workspace build directory')
roots = [p.resolve(strict=True) for p in args.input]
if any(out == p or out.is_relative_to(p) or p.is_relative_to(out) for p in roots):
    parser.error('Output directory overlaps an input')
if args.max_files < 1:
    parser.error('Invalid file limit')
files = sorted({f for root in roots for f in (root.rglob('*.uasset') if root.is_dir() else [root])})[:args.max_files]
out.mkdir(parents=True, exist_ok=True)
cli = args.cli.resolve(strict=True)

class Reader:
    def __init__(self, data, start=0, end=None):
        self.data, self.at, self.end = data, start, len(data) if end is None else end
        self.flag_offsets = []
    def take(self, count):
        if count < 0 or count > self.end - self.at:
            raise ValueError('Reference read exceeds its window')
        value = self.data[self.at:self.at+count]; self.at += count
        return value
    def scalar(self, fmt):
        return struct.unpack('<'+fmt, self.take(struct.calcsize('<'+fmt)))[0]
    def count(self):
        n = self.scalar('i')
        if not 0 <= n <= 1000000:
            raise ValueError('Invalid reference count')
        return n
    def string(self):
        n = self.scalar('i')
        if not n: return ''
        if abs(n) > 16000000: raise ValueError('String limit')
        data = self.take(abs(n)*(2 if n < 0 else 1))
        width = 2 if n < 0 else 1
        if data[-width:] != b'\0'*width: raise ValueError('String terminator')
        return data[:-width].decode('utf-16le' if n < 0 else 'utf-8')
    def boolean(self):
        value = self.scalar('I')
        if value > 1: raise ValueError('Invalid bool')
        return bool(value)
    def name(self):
        index, number = self.scalar('i'), self.scalar('i')
        if index < 0 or index >= len(self.names) or number < 0:
            raise ValueError('Invalid reference FName')
        return self.names[index] + ('_'+str(number-1) if number else '')
    def reference(self):
        index = self.scalar('i')
        def resolve(i, seen):
            if i == 0: return 'None'
            if i in seen or len(seen) >= 256: raise ValueError('Reference cycle')
            seen = seen | {i}
            table = self.imports if i < 0 else self.exports
            row = table[-i-1 if i < 0 else i-1]
            if row['outer']:
                return resolve(row['outer'], seen)+'.'+row['name']
            package = row.get('package', '')
            return package+'.'+row['name'] if package and package != 'None' and not row['name'].startswith('/') else row['name']
        return resolve(index, set())
    def field(self, cooked, depth=0):
        if depth > 64: raise ValueError('Reference depth limit')
        type_name = self.name()
        if type_name == 'None': return None
        name = self.name()
        value = {'name': name, 'type': type_name, 'objectFlags': self.scalar('I'), 'metadata': None}
        if not cooked:
            value['metadata'] = {}
            if self.boolean():
                for _ in range(self.count()):
                    key = self.name(); value['metadata'][key] = self.string()
        for key, fmt in (('arrayDimension','i'), ('elementSize','i'), ('propertyFlags','Q'),
                         ('replicationIndex','H')):
            if key == 'propertyFlags' and depth == 0:
                self.flag_offsets.append({'name':name, 'offset':self.at})
            value[key] = self.scalar(fmt)
        value['replicationNotify'] = self.name()
        value['replicationCondition'] = self.scalar('B')
        if type_name in ('ObjectProperty','WeakObjectProperty','LazyObjectProperty','SoftObjectProperty',
                         'ClassProperty','SoftClassProperty'):
            value['propertyClass'] = self.reference()
            if type_name in ('ClassProperty','SoftClassProperty'): value['metaClass'] = self.reference()
        elif type_name in ('StructProperty','ByteProperty','InterfaceProperty','EnumProperty'):
            key = {'StructProperty':'structure','ByteProperty':'enumeration',
                   'InterfaceProperty':'interfaceClass','EnumProperty':'enumeration'}[type_name]
            value[key] = self.reference()
            if type_name == 'EnumProperty': value['underlying'] = self.field(cooked, depth+1)
        elif 'DelegateProperty' in type_name: value['signatureFunction'] = self.reference()
        elif type_name in ('ArrayProperty','SetProperty','OptionalProperty'):
            value['inner'] = self.field(cooked, depth+1)
        elif type_name == 'MapProperty':
            value['key'] = self.field(cooked, depth+1); value['value'] = self.field(cooked, depth+1)
        elif type_name == 'BoolProperty':
            value['booleanLayout'] = {key:self.scalar('B') for key in
                ('fieldSize','byteOffset','byteMask','fieldMask','boolSize')}
            value['booleanLayout']['nativeBool'] = bool(self.scalar('B'))
        elif type_name == 'FieldPathProperty': value['fieldClass'] = self.name()
        elif type_name not in ('Int8Property','Int16Property','IntProperty','Int64Property','UInt16Property',
                              'UInt32Property','UInt64Property','FloatProperty','DoubleProperty','NameProperty',
                              'StrProperty','TextProperty','Utf8StrProperty','AnsiStrProperty'):
            raise ValueError('Unknown reference field class '+type_name)
        return value

hashes, evidence, failures = {}, [], []
classes, types, versions, unavailable = Counter(), Counter(), Counter(), Counter()
checks = 0
controlled = None
for path in files:
    for source in (path, path.with_suffix('.uexp')):
        if source.is_file(): hashes[str(source)] = hashlib.sha256(source.read_bytes()).hexdigest()
    result = subprocess.run([str(cli), 'inspect', str(path)], capture_output=True, timeout=60)
    if result.returncode:
        failures.append({'file':str(path),'error':result.stderr.decode('utf-8','replace')[:1000]}); continue
    package = json.loads(result.stdout)
    summary = package['summary']; versions[str(summary['ue5Version'])] += 1
    for key, entry in package['coverage'].items():
        owner = package['snapshot']['objects'][key]
        if entry['nativeCoverage'] != 'struct-declaration-prefix':
            if owner['class'].endswith(('GeneratedClass','.ScriptStruct','.Function','.UserDefinedStruct','.Class')):
                unavailable[owner['class']] += 1
            continue
        raw = path.read_bytes()
        reader = Reader(raw, summary['nameOffset'])
        reader.names = []
        for _ in range(summary['nameCount']):
            reader.names.append(reader.string()); reader.take(4)
        reader.at = summary['importOffset']; reader.imports = []
        for _ in range(summary['importCount']):
            reader.name(); reader.name(); outer = reader.scalar('i'); name = reader.name(); package_name = reader.name() if summary['ue4Version'] >= 520 and not summary['flags'] & 0x80000000 else ''
            if summary['ue5Version'] >= 1003: reader.take(4)
            reader.imports.append({'name':name, 'outer':outer, 'package':package_name})
        reader.at = summary['exportOffset']; reader.exports = []
        version = summary['ue5Version']
        export_size = 96 + (16 if version < 1005 else 0) - (4 if version < 1006 else 0) - (4 if version < 1003 else 0) + (16 if version >= 1010 else 0)
        for _ in range(summary['exportCount']):
            start = reader.at; reader.take(12); outer = reader.scalar('i'); name = reader.name()
            reader.exports.append({'name':name, 'outer':outer}); reader.at = start + export_size
        at = entry['offset']
        if at + entry['bytes'] > len(raw):
            raw = path.with_suffix('.uexp').read_bytes(); at -= summary['headerSize']
        reader.data = raw; reader.at = at + entry['nativeStart']; reader.end = at + entry['bytes']
        reader.flag_offsets = []
        cooked = bool(summary['flags'] & 0x200)
        declaration = {'kind':'struct-definition','coverage':'declared-fields',
                       'inheritedFieldsResolved':False,'editorMetadataPresent':not cooked}
        if reader.boolean(): declaration['objectGuid'] = ''.join(f'{reader.scalar("I"):08x}' for _ in range(4))
        declaration['super'] = reader.reference()
        declaration['children'] = [reader.reference() for _ in range(reader.count())]
        fields = [reader.field(cooked) for _ in range(reader.count())]
        declaration['fields'] = {f['name']: f for f in fields}
        declaration['fieldOrder'] = [f['name'] for f in fields]
        assert declaration == owner['native'], (path, key, 'independent declaration mismatch')
        assert reader.at == at + entry['nativeEnd'], (path, key, 'independent boundary mismatch')
        if args.controlled_diff and controlled is None and reader.flag_offsets:
            selected = reader.flag_offsets[0]
            copy_dir = out/'controlled-diff'; copy_dir.mkdir(exist_ok=True)
            copy = copy_dir/path.name
            for source in (path, path.with_suffix('.uexp')):
                if source.is_file(): shutil.copyfile(source, copy_dir/source.name)
            target = copy if entry['offset'] + entry['bytes'] <= path.stat().st_size else copy.with_suffix('.uexp')
            changed = bytearray(target.read_bytes())
            before_flags = struct.unpack_from('<Q', changed, selected['offset'])[0]
            struct.pack_into('<Q', changed, selected['offset'], before_flags ^ 0x10)
            target.write_bytes(changed)
            comparison = subprocess.run([str(cli),'diff',str(path),str(copy)],capture_output=True,timeout=60)
            assert comparison.returncode == 1, comparison.stderr.decode('utf-8','replace')
            delta = json.loads(comparison.stdout)
            escape = lambda text:text.replace('~','~0').replace('/','~1')
            expected_path = '/objects/'+escape(key)+'/native/fields/'+escape(selected['name'])+'/propertyFlags'
            assert len(delta['changes']) == 1 and delta['changes'][0]['path'] == expected_path, delta['changes']
            assert delta['changes'][0]['before'] == before_flags and delta['changes'][0]['after'] == before_flags ^ 0x10
            controlled = {'source':str(path),'copy':str(copy),'changedPath':expected_path,
                          'beforeFlags':before_flags,'afterFlags':before_flags ^ 0x10}
            checks += 3
        classes[owner['class']] += 1
        for field in fields: types[field['type']] += 1
        checks += 2
        evidence.append({'file':str(path),'object':key,'class':owner['class'],'declaredFields':len(fields),
                         'nativeStart':entry['nativeStart'],'nativeEnd':entry['nativeEnd']})
for path, expected in hashes.items():
    assert hashlib.sha256(Path(path).read_bytes()).hexdigest() == expected, 'Original input changed'
report = {'packages':len(files),'checks':checks,'declarations':len(evidence),'classes':dict(classes),
          'fieldTypes':dict(types),'versions':dict(versions),'unavailable':dict(unavailable),
          'failures':failures,'evidence':evidence,'sourceFingerprints':hashes,'originalsUnchanged':True,'controlledDiff':controlled}
(out/'summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
print(json.dumps({k:v for k,v in report.items() if k not in ('failures','evidence','sourceFingerprints')}, indent=2))
if failures or not evidence: raise SystemExit(1)
