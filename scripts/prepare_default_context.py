"""Pin a prepared native CDO and recorded files to an explicit source archive profile."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def file_hash(path, limit):
    size = path.stat().st_size
    if size > limit:
        raise ValueError('Prepared dependency byte budget exceeded')
    h = hashlib.sha256()
    read = 0
    with path.open('rb') as stream:
        while data := stream.read(1024 * 1024):
            read += len(data)
            if read > size:
                raise ValueError('Dependency grew during preparation')
            h.update(data)
    if read != size:
        raise ValueError('Dependency shrank during preparation')
    if path.stat().st_size != size:
        raise ValueError('Dependency changed size during preparation')
    return size, h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--reader', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--copy-inputs', type=Path, help='Copy recorded inputs into a local content-addressed store for pinned replay')
    parser.add_argument('--max-dependency-bytes', type=int, default=8 * 1024**3)
    args = parser.parse_args()
    if args.out.exists():
        parser.error('Refusing an existing context output')
    capture_file = args.capture.resolve(strict=True)
    if capture_file.stat().st_size > 64 * 1024**2:
        parser.error('Capture exceeds its byte budget')
    capture = json.loads(capture_file.read_text(encoding='utf-8'))
    if (capture.get('format') != 'cyuediff.engine-default-capture/1'
            or capture.get('completeCapture') is not True or capture.get('classDefault') is not True
            or capture.get('blueprintClass') is not False):
        parser.error('Expected a complete prepared native class-default capture')
    environment = capture['environment']
    if environment['platform'] != 'Windows' or environment['byteOrder'] != 'little':
        parser.error('Unsupported prepared platform')
    report = subprocess.run([str(args.reader.resolve(strict=True)), 'inspect',
                             str(args.source.resolve(strict=True)), '--metadata'],
                            capture_output=True, check=True)
    source = json.loads(report.stdout)['summary']
    dependencies = {}
    total = 0
    copied = args.copy_inputs.resolve() if args.copy_inputs else None
    if copied:
        copied.mkdir(parents=True, exist_ok=True)

    def stored_path(path, size, digest):
        if not copied:
            return str(path)
        target = copied/(digest+'.bin')
        if target.exists():
            if file_hash(target, size) != (size, digest):
                raise ValueError('Existing pinned input differs from its content address')
        else:
            made = False
            try:
                with path.open('rb') as source_stream, target.open('xb') as target_stream:
                    made = True
                    h = hashlib.sha256()
                    read = 0
                    while data := source_stream.read(1024 * 1024):
                        read += len(data)
                        if read > size:
                            raise ValueError('Input grew while pinning')
                        target_stream.write(data)
                        h.update(data)
                if read != size or h.hexdigest() != digest:
                    raise ValueError('Input changed while pinning')
            except BaseException:
                if made:
                    target.unlink()
                raise
        try:
            return os.path.relpath(target, args.out.resolve().parent)
        except ValueError:
            return str(target)

    files = environment['files']
    if len(files) > 4096:
        parser.error('Prepared dependency count exceeded')
    for key, node in files.items():
        path = Path(node['file'])
        if not node['present']:
            if node['role'] != 'config' or path.exists():
                parser.error('Prepared configuration absence changed')
            dependencies[key] = {'file': str(path), 'absent': True}
            continue
        size, digest = file_hash(path.resolve(strict=True), args.max_dependency_bytes - total)
        total += size
        if str(size) != node['bytes'] or digest != node['sha256']:
            parser.error(f'Recorded input {key} changed after native capture; prepare a fresh CDO')
        dependencies[key] = {'file': stored_path(path, size, digest), 'bytes': size, 'sha256': digest}
    archive = capture['archive']
    capture_size, capture_digest = file_hash(capture_file, 64 * 1024**2)
    prepared = {
        'format': 'cyuediff.default-context/1',
        'inputMode': 'pinned-inputs' if copied else 'local-inputs',
        'snapshot': {'file': stored_path(capture_file, capture_size, capture_digest), 'sha256': capture_digest,
                     'engineVersion': capture['engineVersion'], 'filterEditorOnly': capture['filterEditorOnly'],
                     'textLayout': 'ue5-history',
                     'nativeStructs': {name: layout for name, layout in [
                         ('/Script/CoreUObject.IntPoint', 'int-point'),
                         ('/Script/CoreUObject.Vector', 'vector3-f64'),
                         ('/Script/CoreUObject.Quat', 'quat-f64')]
                         if name in capture['declarations']}},
        'sourceProfile': {'profile': 'ue5.3.2-win64-tagged-ascii/1', 'savedEngine': source['savedEngine'],
                          'ue4': source['ue4Version'], 'ue5': source['ue5Version'],
                          'licensee': source['licenseeVersion'], 'customVersions': source['customVersions'],
                          'intraPropertyDelta': True},
        'dependencies': dependencies}
    if archive['ue4'] != 522 or archive['ue5'] != 1009:
        parser.error('Unsupported captured archive')
    with args.out.open('x', encoding='utf-8', newline='\n') as stream:
        json.dump(prepared, stream, ensure_ascii=False, indent=2)
        stream.write('\n')
    print(f'Prepared {len(dependencies)} recorded inputs ({total} file bytes). No Engine code executed.')


if __name__ == '__main__':
    main()
