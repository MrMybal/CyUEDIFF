"""Validate explicit host-owned Pak/IoStore inputs against an independent byte reader.

Reports and extracted samples stay in an ignored workspace build directory.
The original containers are fingerprinted before and after. No engine is launched.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--input', type=Path, action='append', required=True)
parser.add_argument('--codec', type=Path)
parser.add_argument('--cli', type=Path, default=Path('build-native/Release/cyuediff.exe'))
parser.add_argument('--output-dir', type=Path, default=Path('build-native/container-validation'))
args = parser.parse_args()
workspace = Path(__file__).resolve().parents[1]
out = args.output_dir.resolve()
try:
    relative = out.relative_to(workspace)
except ValueError:
    parser.error('Outputs must stay in an ignored workspace build directory')
if not relative.parts or not relative.parts[0].startswith('build'):
    parser.error('Invalid report directory')
inputs = [p.resolve(strict=True) for p in args.input]
if any(out == p.parent or out.is_relative_to(p.parent) for p in inputs):
    parser.error('Output directory overlaps the input container directory')
out.mkdir(parents=True, exist_ok=True)
cli = str(args.cli.resolve(strict=True))
codec_flags = []
module = None
if args.codec:
    codec_path = args.codec.resolve(strict=True)
    codec_flags = ['--codec', str(codec_path)]
    module = ctypes.CDLL(str(codec_path))
    decode_fn = module.cyuediff_decode_block_v1
    decode_fn.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p, ctypes.c_size_t]
    decode_fn.restype = ctypes.c_size_t


def decode(name, data, size):
    if name.lower() == 'none':
        assert len(data) == size
        return data
    assert module and name.lower() in ('oodle', 'lz4'), 'Explicit compatible codec is required'
    target = ctypes.create_string_buffer(size)
    source = ctypes.create_string_buffer(data)
    assert decode_fn(3 if name.lower() == 'oodle' else 4, source, len(data), target, size) == size
    return target.raw


class Reader:
    def __init__(self, data, pos=0):
        self.data, self.pos = data, pos

    def take(self, count):
        assert count >= 0 and self.pos + count <= len(self.data)
        value = self.data[self.pos:self.pos+count]
        self.pos += count
        return value

    def num(self, count=4, big=False):
        return int.from_bytes(self.take(count), 'big' if big else 'little')

    def string(self):
        count = struct.unpack('<i', self.take(4))[0]
        return self.take(abs(count)*(2 if count < 0 else 1)).decode('utf-16-le' if count < 0 else 'utf-8').rstrip('\0')


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024*1024), b''):
            h.update(block)
    return h.hexdigest()


def command(*command_args, code=0):
    result = subprocess.run([cli, *map(str, command_args)], capture_output=True, timeout=240)
    assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
    return json.loads(result.stdout if code != 2 else result.stderr)


def io_hashes(path):
    b = path.read_bytes()
    header, count, blocks, entry_size, names, width, block_size, directory_size, partitions = struct.unpack_from('<9I', b, 20)
    assert header == 144 and entry_size == 12 and not b[80] & (2 | 4)
    seeds = struct.unpack_from('<I', b, 84)[0]
    partition_size = struct.unpack_from('<Q', b, 88)[0]
    overflow = struct.unpack_from('<I', b, 96)[0]
    ids = [b[header+j*12:header+(j+1)*12].hex() for j in range(count)]
    offsets = Reader(b, header+count*12)
    chunks = [(offsets.num(5, True), offsets.num(5, True)) for _ in ids]
    blocks_at = header+count*22+(seeds+overflow)*4
    r = Reader(b, blocks_at)
    table = [(r.num(5), r.num(3), r.num(3), r.num(1)) for _ in range(blocks)]
    methods = ['None'] + [r.take(width).split(b'\0', 1)[0].decode('ascii') for _ in range(names)]
    paths = [path.with_name(path.stem+('' if j == 0 else f'_s{j}')+'.ucas') for j in range(partitions)]
    result = {}
    streams = [p.open('rb') for p in paths]
    try:
        for chunk_id, (offset, size) in zip(ids, chunks):
            h = hashlib.sha256()
            while size:
                bi, inside = divmod(offset, block_size)
                physical, stored, raw, method = table[bi]
                partition, local = divmod(physical, partition_size)
                streams[partition].seek(local)
                data = decode(methods[method], streams[partition].read(stored), raw)
                take = min(size, len(data)-inside)
                assert take > 0
                h.update(data[inside:inside+take])
                offset += take
                size -= take
            result[chunk_id] = h.hexdigest()
    finally:
        for stream in streams:
            stream.close()
    return result


def pak_hashes(path):
    b = path.read_bytes()
    footer = Reader(b, len(b)-221)
    footer.take(17)
    assert footer.num() == 0x5a6f12e1 and footer.num() in (10, 11)
    at, size = footer.num(8), footer.num(8)
    assert hashlib.sha1(b[at:at+size]).digest() == footer.take(20)
    methods = ['None']+[footer.take(32).split(b'\0', 1)[0].decode('ascii') for _ in range(5)]
    index = Reader(b[at:at+size])
    index.string()
    count = index.num()
    index.take(8)
    full = None
    for which in ('hash', 'full'):
        if index.num():
            offset, size, sha = index.num(8), index.num(8), index.take(20)
            if which == 'full':
                full = b[offset:offset+size]
                assert hashlib.sha1(full).digest() == sha
    encoded = Reader(index.take(index.num()))

    def entry(r):
        offset, stored, raw, method = r.num(8), r.num(8), r.num(8), r.num()
        sha = r.take(20)
        blocks = [(r.num(8), r.num(8)) for _ in range(r.num())] if method else []
        flags, block_size = r.num(1), r.num()
        return offset, stored, raw, method, sha, blocks, flags, block_size

    regular = [entry(index) for _ in range(index.num())]
    directory = Reader(full)
    result = {}
    total = 0
    for _ in range(directory.num()):
        prefix = directory.string()
        if prefix == '/':
            prefix = ''
        for _ in range(directory.num()):
            name = prefix+directory.string()
            loc = struct.unpack('<i', directory.take(4))[0]
            total += 1
            if loc == -2147483648:
                continue
            if loc < 0:
                offset = regular[-loc-1][0]
            else:
                encoded.pos = loc
                bits = encoded.num()
                if bits & 63 == 63:
                    encoded.num()
                offset = encoded.num(4 if bits & 0x80000000 else 8)
            payload = Reader(b, offset)
            _, stored, raw, method, sha, blocks, flags, block_size = entry(payload)
            assert not flags & 3
            assert hashlib.sha1(b[payload.pos:payload.pos+stored]).digest() == sha
            if method:
                decoded = bytearray()
                for start, end in blocks:
                    decoded.extend(decode(methods[method], b[offset+start:offset+end], min(block_size, raw-len(decoded))))
                assert len(decoded) == raw
                result[name] = hashlib.sha256(decoded).hexdigest()
            else:
                result[name] = hashlib.sha256(b[payload.pos:payload.pos+raw]).hexdigest()
    assert total == count
    return result


reports = []
for number, path in enumerate(inputs):
    inventory = command('container', path)
    sources = [Path(p) for p in inventory['files']]
    fingerprints = {str(p): digest(p) for p in sources}
    manifest = command('container-hash', path, *codec_flags)
    expected = io_hashes(path) if path.suffix.lower() == '.utoc' else pak_hashes(path)
    measured = {e['id']: e['sha256'] for e in manifest['decodedSha256']}
    assert expected == measured, 'Independent decoded member hashes differ'
    (out/f'{number}-manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    same = command('container-diff', path, path, *codec_flags)
    assert same['status'] == 'identical' and same['unchanged'] == len(measured)
    sample = next(e for e in inventory['entries'] if not e['deleted'])
    destination = out/f'{number}-sample.bin'
    read = command('container-read', path, sample['id'], '--out', destination, *codec_flags)
    assert read['sha256'] == digest(destination) == measured[sample['id']]
    refused = 0
    for source in sources:
        command('container-read', path, sample['id'], '--out', source, *codec_flags, code=2)
        command('container', path, '--out', source, code=2)
        refused += 2
    # Truncate only copies, never the user-supplied inputs.
    for length in (0, 16, 128):
        truncated = out/f'{number}-truncated-{length}{path.suffix}'
        truncated.write_bytes(path.read_bytes()[:length])
        command('container', truncated, code=2)
        refused += 1
    assert all(digest(Path(p)) == sha for p, sha in fingerprints.items()), 'Original source changed'
    reports.append({'input': str(path), 'entries': len(measured), 'decodedBytes': sum(e['size'] for e in inventory['entries'] if not e['deleted']),
                    'independentHashesMatch': True, 'refusals': refused, 'sourceFingerprints': fingerprints})
(out/'summary.json').write_text(json.dumps(reports, indent=2), encoding='utf-8')
print(json.dumps([{k: v for k, v in report.items() if k != 'sourceFingerprints'} for report in reports], indent=2))
