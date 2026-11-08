"""Build sparse cooked USMAP mappings from explicit, complete UHT metadata.

This module never starts Unreal or treats a native C++ struct as a reflected wire
layout. Missing type descriptions retain their slots; unknown static dimensions
exclude their declaring type and derived types instead of shifting later fields.
"""
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import struct

def build_cooked_mappings(metadata_directory: Path, output_file: Path, summary_file: Path | None=None, static_dimensions: Path | None=None):
    records = {}
    aliases = defaultdict(set)
    enums = {}
    excluded = {}
    unsupported = []
    TYPE_NAMES = 'Byte Bool Int Float Object Name Delegate Double Array Struct Str Text Interface MulticastDelegate WeakObject LazyObject AssetObject SoftObject UInt64 UInt32 UInt16 Int64 Int16 Int8 Map Set Enum FieldPath Optional Utf8Str AnsiStr'.split()
    KINDS = {name + 'Property': i for i, name in enumerate(TYPE_NAMES)}
    ALIASES = {'LargeWorldCoordinatesRealProperty':'DoubleProperty', 'ObjectPtrProperty': 'ObjectProperty', 'ClassProperty': 'ObjectProperty', 'ClassPtrProperty': 'ObjectProperty', 'SoftClassProperty': 'SoftObjectProperty', 'MulticastInlineDelegateProperty': 'MulticastDelegateProperty', 'MulticastSparseDelegateProperty': 'MulticastDelegateProperty'}

    def ascii_name(name):
        if not isinstance(name, str):
            raise ValueError('Expected a string mapping name')
        data = name.encode('ascii')
        if not data or len(data) > 65535 or b'\x00' in data:
            raise ValueError('Invalid mapping name')
        return data
    nodes = 0

    def visit(node, module, depth=0):
        nonlocal nodes
        if not isinstance(node, dict) or not isinstance(node.get('Children', []), list):
            raise ValueError('Malformed UHT metadata node')
        nodes += 1
        if nodes > 200000 or depth > 64:
            raise ValueError('Metadata node/depth limit exceeded')
        kind = node.get('EngineClassName', '')
        if kind in ('Class', 'ScriptStruct', 'Enum') and node.get('ClassType') != 'NativeInterface':
            name = node['EngineName']
            identity = module + '.' + name
            target = enums if kind == 'Enum' else records
            if identity in target:
                raise ValueError('Duplicate qualified UHT identity: ' + identity)
            target[identity] = {'node': node, 'module': module, 'name': name, 'identity': identity}
            for alias in {node['SourceName'], node.get('CppType', node['SourceName'])}:
                aliases[alias].add(identity)
        for child in node.get('Children', []):
            visit(child, module, depth + 1)
    overrides = {}
    dimensions_hash = None
    if static_dimensions is not None:
        if static_dimensions.stat().st_size > 2*1024*1024:
            raise ValueError('Static dimension manifest exceeds byte limit')
        dimensions_raw=static_dimensions.read_bytes()
        dimensions_hash=hashlib.sha256(dimensions_raw).hexdigest()
        dimensions=json.loads(dimensions_raw.decode('utf-8'))
        if not isinstance(dimensions,dict) or set(dimensions)!={'format','metadataSha256','fields'} or dimensions['format']!='cyuediff.uht-dimensions/1':
            raise ValueError('Invalid static dimension manifest')
        overrides=dimensions['fields']
        if not isinstance(overrides,dict) or len(overrides)>100000:
            raise ValueError('Invalid static dimension fields')
        for key,value in overrides.items():
            if not isinstance(key,str) or not key.startswith('/Script/') or len(key)>4096 or not isinstance(value,dict) or set(value)!={'expression','size'}:
                raise ValueError('Invalid static dimension binding')
            if not isinstance(value['expression'],str) or not 1<=len(value['expression'])<=1024 or type(value['size']) is not int or not 1<=value['size']<=255:
                raise ValueError('Invalid static dimension value')
    files = sorted(metadata_directory.rglob('*.cyuediff.json'))
    if not files:
        raise ValueError('No CyUEDiff UHT metadata found')
    for destination in (output_file, summary_file):
        if destination is None:
            continue
        if any(destination.resolve() == path.resolve() or (destination.exists() and destination.samefile(path)) for path in (files + ([static_dimensions] if static_dimensions is not None else []))):
            raise ValueError('Output overlaps metadata or dimension source')
    if summary_file is not None and output_file.exists() and summary_file.exists() and output_file.samefile(summary_file):
        raise ValueError('Output files overlap')
    if summary_file is not None and summary_file.resolve() == output_file.resolve():
        raise ValueError('Output files overlap')
    if sum((path.stat().st_size for path in files)) > 256 * 1024 * 1024:
        raise ValueError('Metadata byte limit exceeded')
    source_hashes = {}
    for path in files:
        if path.stat().st_size > 64 * 1024 * 1024:
            raise ValueError('Metadata module byte limit exceeded')
        raw = path.read_bytes()
        source_hashes[path.relative_to(metadata_directory).as_posix()] = hashlib.sha256(raw).hexdigest()
        data = json.loads(raw.decode('utf-8'))
        if data.get('schema') != 'cyuediff.uht/1' or data.get('EngineClassName') != 'Package':
            raise ValueError('Expected complete CyUEDiff UHT metadata')
        if not data['SourceName'].startswith('/Script/') or '.' in data['SourceName']:
            raise ValueError('Invalid UHT module identity')
        visit(data, data['SourceName'])

    metadata_sha256=hashlib.sha256(json.dumps(source_hashes,sort_keys=True,separators=(',',':')).encode('utf-8')).hexdigest()
    if static_dimensions is not None and dimensions['metadataSha256']!=metadata_sha256:
        raise ValueError('Static dimension manifest does not match this exact metadata export')
    used_dimensions=set()

    def resolve(source):
        if not source:
            return ''
        found = aliases[source]
        if len(found) != 1:
            raise ValueError('Missing or ambiguous UHT type: ' + source)
        return next(iter(found))
    for identity, record in records.items():
        node = record['node']
        fields = []
        slots = 0
        try:
            ascii_name(record['name'])
            parent = resolve(node.get('Super', ''))
            for child in node.get('Children', []):
                if not child.get('EngineClassName', '').endswith('Property'):
                    continue
                if 'EditorOnly' in child.get('PropertyFlags', '').split(', '):
                    continue
                expression = child.get('ArrayDimensions', '1')
                key=identity+'.'+child['EngineName']
                if key in overrides:
                    binding=overrides[key]
                    if binding['expression']!=str(expression):
                        raise ValueError('Static dimension expression disagrees with metadata: '+key)
                    dim=binding['size']
                    if str(expression).isdigit() and not str(expression).startswith('0') and dim!=int(expression):
                        raise ValueError('Static dimension contradicts a known literal: '+key)
                    used_dimensions.add(key)
                else:
                    if not str(expression).isdigit() or str(expression).startswith('0') or not 1 <= int(expression) <= 255:
                        raise ValueError('Unresolved static array dimension ' + str(expression))
                    dim = int(expression)
                fields.append((slots, dim, child))
                slots += dim
            if slots > 65535:
                raise ValueError('Slot count exceeds USMAP')
            record.update(parent=parent, fields=fields, slots=slots)
        except (ValueError, UnicodeError) as error:
            if str(error).startswith('Static dimension '): raise
            excluded[identity] = str(error)
    if used_dimensions!=set(overrides):
        raise ValueError('Unused or unreachable static dimension binding')
    changed = True
    while changed:
        changed = False
        for identity, record in records.items():
            if identity not in excluded and record.get('parent') and (record['parent'] in excluded or record['parent'] not in records):
                excluded[identity] = 'Unavailable base ' + record['parent']
                changed = True
    for identity in records:
        if identity in excluded:
            continue
        cursor = identity
        chain = set()
        while cursor:
            if cursor in chain or len(chain) > 48:
                raise ValueError('Cyclic or excessive schema inheritance')
            chain.add(cursor)
            cursor = records[cursor]['parent']
    schemas = [r for key, r in sorted(records.items()) if key not in excluded]
    enum_records = []
    for key, record in sorted(enums.items()):
        try:
            ascii_name(record['name'])
            for item in record['node'].get('EnumValues', []):
                ascii_name(item['Name'])
            enum_records.append(record)
        except UnicodeError:
            pass
    available_enums = {r['identity'] for r in enum_records}

    def descriptor(field, depth=0):
        if not isinstance(field, dict):
            raise ValueError('Missing nested property description')
        if depth > 48:
            raise ValueError('Type depth exceeded')
        original = field.get('EngineClassName', '')
        kind = ALIASES.get(original, original)
        if kind not in KINDS:
            raise ValueError('Unsupported field ' + kind)
        result = {'kind': KINDS[kind]}
        if kind == 'StructProperty':
            identity = resolve(field['ScriptStruct'])
            if identity in excluded:
                raise ValueError('Unavailable structure ' + identity)
            result['name'] = identity
        elif kind == 'EnumProperty':
            enum = resolve(field['Enum'])
            if enum not in available_enums:
                raise ValueError('Unavailable enum ' + enum)
            underlying = {'Uint8': 'Byte', 'UInt8': 'Byte', 'Int8': 'Int8', 'Int16': 'Int16', 'Uint16': 'UInt16', 'UInt16': 'UInt16', 'Int': 'Int', 'Int32': 'Int', 'Uint32': 'UInt32', 'UInt32': 'UInt32', 'Int64': 'Int64', 'Uint64': 'UInt64', 'UInt64': 'UInt64'}[field['UnderlyingType']]
            result.update(name=enum, inner={'kind': KINDS[underlying + 'Property']})
        elif kind in ('ArrayProperty', 'SetProperty', 'OptionalProperty'):
            result['inner'] = descriptor(field['ValueProperty'], depth + 1)
        elif kind == 'MapProperty':
            result.update(key=descriptor(field['KeyProperty'], depth + 1), value=descriptor(field['ValueProperty'], depth + 1))
        return result
    names = []
    ids = {}

    def name(value):
        if value == '':
            return struct.pack('<i', -1)
        ascii_name(value)
        if value not in ids:
            ids[value] = len(names)
            names.append(value)
        return struct.pack('<i', ids[value])

    def type_bytes(value):
        kind = value['kind']
        data = bytes([kind])
        if kind == 26:
            return data + type_bytes(value['inner']) + name(value['name'])
        if kind == 9:
            return data + name(value['name'])
        if kind in (8, 25, 28):
            return data + type_bytes(value['inner'])
        if kind == 24:
            return data + type_bytes(value['key']) + type_bytes(value['value'])
        return data
    payload = bytearray(struct.pack('<I', len(enum_records)))
    for record in enum_records:
        values = record['node'].get('EnumValues', [])
        if len(values) > 65535:
            raise ValueError('Enum table exceeds USMAP limits')
        payload += name(record['name']) + struct.pack('<H', len(values))
        for item in values:
            value = int(item['Value'])
            if not -(1 << 63) <= value < 1 << 64:
                raise ValueError('Enum value exceeds its stored 64-bit range')
            payload += struct.pack('<Q', value & 18446744073709551615) + name(item['Name'])
    payload += struct.pack('<I', len(schemas))
    for record in schemas:
        encoded = []
        for index, dimension, field in record['fields']:
            try:
                encoded.append(struct.pack('<HB', index, dimension) + name(field['EngineName']) + type_bytes(descriptor(field)))
            except (KeyError, ValueError, UnicodeError) as error:
                unsupported.append({'schema': record['identity'], 'field': field['EngineName'], 'index': index, 'reason': str(error)})
        payload += name(record['name']) + name(record['parent']) + struct.pack('<HH', record['slots'], len(encoded)) + b''.join(encoded)
    paths = bytearray(b'\x00' + struct.pack('<I', len(enum_records)))
    for record in enum_records:
        paths += name(record['module'])
    paths += struct.pack('<I', len(schemas))
    for record in schemas:
        paths += name(record['module'])
    payload += b'CEXT' + b'\x00' + struct.pack('<I', 1) + b'PPTH' + struct.pack('<I', len(paths)) + paths
    name_table = struct.pack('<I', len(names)) + b''.join((struct.pack('<H', len(ascii_name(n))) + ascii_name(n) for n in names))
    body = name_table + payload
    result = struct.pack('<HBiBII', 12484, 4, 0, 0, len(body), len(body)) + body
    if any(hashlib.sha256(path.read_bytes()).hexdigest() != source_hashes[path.relative_to(metadata_directory).as_posix()] for path in files):
        raise ValueError('Metadata changed during mapping generation')
    if static_dimensions is not None and hashlib.sha256(static_dimensions.read_bytes()).hexdigest()!=dimensions_hash:
        raise ValueError('Static dimension manifest changed during generation')
    output_file.write_bytes(result)
    summary = {'schemas': len(schemas), 'enums': len(enum_records), 'names': len(names), 'bytes': len(result), 'excludedClasses': excluded, 'unsupportedDescriptors': unsupported, 'sha256': hashlib.sha256(result).hexdigest(), 'metadataFiles': source_hashes, 'metadataSha256':metadata_sha256}
    if static_dimensions is not None:
        summary['staticDimensions']={'sha256':dimensions_hash,'fields':overrides}
    if summary_file is not None:
        summary_file.write_text(json.dumps(summary, indent=2), encoding='utf-8')
    return summary
