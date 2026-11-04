"""Independently validate a host-owned UE 5.3 Zen corpus and an opaque per-object edit.
No original package/container/engine file is modified; reports stay in an ignored build directory.
"""
import argparse
from collections import Counter
import ctypes
import functools
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--container', type=Path, required=True)
parser.add_argument('--global-container', type=Path, required=True)
parser.add_argument('--codec', type=Path, required=True)
parser.add_argument('--cli', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/zen-validation'))
args = parser.parse_args()
workspace = Path(__file__).resolve().parents[1]
out = args.output_dir.resolve()
try:
    relative = out.relative_to(workspace)
except ValueError:
    parser.error('Outputs must stay in an ignored workspace build directory')
if not relative.parts or not relative.parts[0].startswith('build'):
    parser.error('Invalid report directory')
inputs = [args.container.resolve(strict=True), args.global_container.resolve(strict=True)]
if any(out == p.parent or out.is_relative_to(p.parent) for p in inputs):
    parser.error('Output directory overlaps original containers')
out.mkdir(parents=True, exist_ok=True)
cli = str(args.cli.resolve(strict=True))
codec = args.codec.resolve(strict=True)
module = ctypes.CDLL(str(codec))
decode = module.cyuediff_decode_block_v1
decode.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
decode.restype = ctypes.c_size_t


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as source:
        for block in iter(lambda: source.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def command(*values, code=0):
    result = subprocess.run([cli, *map(str, values)], capture_output=True, timeout=240)
    assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
    return json.loads(result.stderr if code == 2 else result.stdout)


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def num(self, n=4, big=False, signed=False):
        assert self.pos+n <= len(self.data)
        value = int.from_bytes(self.data[self.pos:self.pos+n], 'big' if big else 'little', signed=signed)
        self.pos += n
        return value

    def names(self):
        count = self.num()
        if not count:
            return []
        size = self.num()
        self.pos += 8+count*8
        headers = [self.num(2, True) for _ in range(count)]
        start = self.pos
        names = []
        for header in headers:
            wide, length = bool(header & 0x8000), header & 0x7fff
            end = self.pos+length*(2 if wide else 1)
            names.append(self.data[self.pos:end].decode('utf-16-le' if wide else 'latin-1'))
            self.pos = end
        assert self.pos == start+size
        return names


def mapped(index, number, names):
    return names[index & 0x3fffffff]+('' if not number else f'_{number-1}')


class Io:
    def __init__(self, path):
        b = path.read_bytes()
        header, n, nb, stride, nm, width, self.block_size, _, partitions = struct.unpack_from('<9I', b, 20)
        assert header == 144 and stride == 12 and b[16] == 5 and not b[80] & (2 | 4)
        self.partition_size = struct.unpack_from('<Q', b, 88)[0]
        seeds = struct.unpack_from('<I', b, 84)[0]
        overflow = struct.unpack_from('<I', b, 96)[0]
        ids = [b[header+j*12:header+(j+1)*12].hex() for j in range(n)]
        r = Reader(b, header+n*12)
        self.chunks = {key: (r.num(5, True), r.num(5, True)) for key in ids}
        r.pos = header+n*22+(seeds+overflow)*4
        self.blocks = [(r.num(5), r.num(3), r.num(3), r.num(1)) for _ in range(nb)]
        self.methods = ['None']+[b[r.pos+j*width:r.pos+(j+1)*width].split(b'\0', 1)[0].decode('ascii') for j in range(nm)]
        self.paths = [path, *[path.with_name(path.stem+('' if j == 0 else f'_s{j}')+'.ucas') for j in range(partitions)]]
        self.streams = [p.open('rb') for p in self.paths[1:]]

    def read(self, key):
        offset, size = self.chunks[key]
        output = bytearray()
        while size:
            bi, inside = divmod(offset, self.block_size)
            physical, stored, raw, method = self.blocks[bi]
            part, at = divmod(physical, self.partition_size)
            stream = self.streams[part]
            stream.seek(at)
            source = stream.read(stored)
            if method:
                assert self.methods[method] == 'Oodle'
                target = ctypes.create_string_buffer(raw)
                data = ctypes.create_string_buffer(source)
                assert decode(3, data, stored, target, raw) == raw
                source = target.raw
            take = min(size, raw-inside)
            assert take > 0
            output.extend(source[inside:inside+take])
            offset += take
            size -= take
        return output

    def close(self):
        for stream in self.streams:
            stream.close()


def scripts(data):
    r = Reader(data)
    names = r.names()
    entries = {}
    for _ in range(r.num()):
        index, number, key, outer, _ = r.num(), r.num(), r.num(8), r.num(8), r.num(8)
        entries[key] = (mapped(index, number, names), outer)

    @functools.lru_cache(None)
    def path(key):
        name, outer = entries[key]
        return name if outer == 0xffffffffffffffff else path(outer)+'.'+name
    return {key: path(key) for key in entries}


def package(data, global_paths):
    r = Reader(data)
    versioned, header, package_index, package_number, flags, _ = [r.num() for _ in range(6)]
    offsets = [r.num() for _ in range(7)]
    assert not versioned, 'This corpus validator targets the supplied unversioned UE 5.3 build'
    names = r.names()
    name = mapped(package_index, package_number, names)
    bulk = []
    for _ in range(r.num(8)//32):
        at, duplicate, size, bulk_flags, cooked_index = r.num(8, signed=True), r.num(8, signed=True), r.num(8, signed=True), r.num(), r.num(1)
        r.pos += 3
        bulk.append(dict(offset=at, duplicateOffset=duplicate, size=size, flags=bulk_flags, cookedIndex=cooked_index))
    assert r.pos == offsets[0]
    hashes = [r.num(8) for _ in range((offsets[1]-offsets[0])//8)]
    imports = [r.num(8) for _ in range((offsets[2]-offsets[1])//8)]
    exports = []
    for _ in range((offsets[3]-offsets[2])//72):
        at, size, index, number = r.num(8), r.num(8), r.num(), r.num()
        outer, cls, parent, template, public = [r.num(8) for _ in range(5)]
        object_flags, filtered = r.num(), r.num(1)
        r.pos += 3
        exports.append(dict(offset=header+at, size=size, name=mapped(index, number, names), outer=outer, cls=cls,
                            parent=parent, template=template, public=public, flags=object_flags, filtered=filtered))
    r.pos = offsets[6]
    imported_names = r.names()
    imported_names = [mapped(j, r.num(), imported_names) for j in range(len(imported_names))]

    def external(key):
        if key == 0xffffffffffffffff:
            return ''
        if key >> 62 == 1:
            return global_paths.get(key, f'$script:{key:016x}')
        index = (key & 0x3fffffffffffffff) >> 32
        pkg = imported_names[index] if imported_names else f'$package-index:{index}'
        return pkg+f'#{hashes[key & 0xffffffff]:016x}'

    @functools.lru_cache(None)
    def object_path(index):
        e = exports[index]
        outer = e['outer']
        prefix = name if outer == 0xffffffffffffffff else (object_path(outer) if outer >> 62 == 0 else external(outer))
        return prefix+'.'+e['name']

    def ref(key):
        if key == 0xffffffffffffffff:
            return None
        tag = key >> 62
        if tag == 0:
            return dict(kind='export', path=object_path(key))
        if tag == 1:
            return dict(kind='script', path=global_paths[key], resolved=True) if key in global_paths else dict(kind='script', id=f'{key:016x}', resolved=False)
        package_index = (key & 0x3fffffffffffffff) >> 32
        result = dict(kind='package', publicExportHash=f'{hashes[key & 0xffffffff]:016x}')
        result['package' if imported_names else 'packageIndex'] = imported_names[package_index] if imported_names else package_index
        return result

    objects = {}
    for j, e in enumerate(exports):
        cls = object_path(e['cls']) if e['cls'] >> 62 == 0 else external(e['cls'])
        objects[object_path(j)] = dict(name=e['name'], **{'class': cls}, flags=e['flags'], filterFlags=e['filtered'],
            references=dict(outer=ref(e['outer']), **{'class': ref(e['cls'])}, super=ref(e['parent']), template=ref(e['template'])),
            publicExportHash=f'{e["public"]:016x}', opaque=dict(size=e['size'], sha256=hashlib.sha256(data[e['offset']:e['offset']+e['size']]).hexdigest()))
    return name, flags, objects, exports, bulk, [ref(key) for key in imports]


def raw_container(path, key, data):
    block_size = 65536
    count = (len(data)+block_size-1)//block_size
    header = bytearray(144)
    header[:16] = b'-==--==--==--==-'
    header[16] = 5
    struct.pack_into('<9I', header, 20, 144, 1, count, 12, 0, 32, block_size, 0, 1)
    struct.pack_into('<Q', header, 88, 0xffffffffffffffff)
    toc = header+bytes.fromhex(key)+bytes(5)+len(data).to_bytes(5, 'big')
    for j in range(count):
        at, size = j*block_size, min(block_size, len(data)-j*block_size)
        toc += at.to_bytes(5, 'little')+size.to_bytes(3, 'little')*2+bytes(1)
    path.write_bytes(toc+bytes(33))
    path.with_suffix('.ucas').write_bytes(data)


io, glob = Io(inputs[0]), Io(inputs[1])
try:
    sources = set(io.paths+glob.paths)
    fingerprints = {str(p): digest(p) for p in sources}
    global_id = next(key for key in glob.chunks if key.endswith('05'))
    global_data = glob.read(global_id)
    global_paths = scripts(global_data)
    flags = ['--cooked-profile', 'ue5.3', '--script-objects', inputs[1], '--codec', codec]
    catalog_path = out/'catalog.json'
    result = subprocess.run([cli, 'zen-catalog', str(inputs[0]), *map(str, flags), '--snapshot', '--out', str(catalog_path)], capture_output=True, timeout=240)
    assert result.returncode == 0, result.stderr.decode('utf-8', 'replace')
    catalog = json.loads(catalog_path.read_text(encoding='utf-8'))
    assert catalog['complete'] and catalog['scriptObjectCount'] == len(global_paths)
    assert catalog['scriptObjectsSha256'] == hashlib.sha256(global_data).hexdigest()
    counts = Counter()
    selected = None
    exports_checked = 0
    for item in catalog['packages']:
        data = io.read(item['id'])
        name, package_flags, objects, exports, bulk, imports = package(data, global_paths)
        snapshot = item['snapshot']
        assert item['name'] == snapshot['package'] == name
        assert snapshot['objects'] == objects, f'Independent object metadata/hash mismatch: {name}'
        assert snapshot['bulkData'] == bulk and snapshot['imports'] == imports and snapshot['flags'] == package_flags
        assert hashlib.sha256(data).hexdigest() == item['sha256']
        assert item['classes'] == dict(Counter(e['class'] for e in objects.values()))
        assert item['exports'] == len(exports)
        assert item['unversionedProperties'] == bool(package_flags & 0x2000)
        counts.update(item['classes'])
        exports_checked += len(exports)
        if selected is None and 'BP_CyTrace_Demo_' in name:
            for path, export in zip(objects, exports):
                if export['size']:
                    selected = item, data, path, export
                    break
    assert catalog['exportCount'] == exports_checked
    assert catalog['packageCount'] == len([key for key in io.chunks if key.endswith('01')])
    assert selected, 'A representative real Blueprint is required for the controlled copy edit'
    item, original, object_path, export = selected
    modified = bytearray(original)
    modified[export['offset']] ^= 1
    before, after = out/'before.utoc', out/'after.utoc'
    raw_container(before, item['id'], original)
    raw_container(after, item['id'], modified)
    diff = command('zen-diff', before, item['id'], after, item['id'], *flags, code=1)
    escaped = object_path.replace('~', '~0').replace('/', '~1')
    assert len(diff['changes']) == 1 and diff['changes'][0]['path'] == '/objects/'+escaped+'/opaque/sha256'
    assert diff['binaryChanged'] and not diff['propertiesDecoded'] and not diff['externalBulkCompared']
    (out/'controlled-diff.json').write_text(json.dumps(diff, indent=2), encoding='utf-8')
    assert command('zen-diff', before, item['id'], before, item['id'], *flags)['status'] == 'identical'
    refusals = 0
    for source in sources:
        error = command('zen-inspect', inputs[0], item['id'], *flags, '--out', source, code=2)
        assert error['error'] == 'output-path'
        refusals += 1
    for length in (0, 16, 51):
        truncated = out/f'truncated-{length}.utoc'
        truncated.write_bytes(inputs[0].read_bytes()[:length])
        command('zen-catalog', truncated, *flags, code=2)
        refusals += 1
    assert all(digest(Path(p)) == sha for p, sha in fingerprints.items()), 'Original source changed'
    report = dict(packages=catalog['packageCount'], exports=exports_checked, globalScriptObjects=len(global_paths),
        unversionedPackages=catalog['unversionedPackages'], independentObjectsMatch=True, classes=dict(counts),
        controlledOpaqueObject=object_path, controlledChanges=len(diff['changes']), refusals=refusals,
        sourceFingerprints=fingerprints)
    (out/'summary.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: v for k, v in report.items() if k not in ('sourceFingerprints', 'classes')}, indent=2))
finally:
    io.close()
    glob.close()
