"""Run only in the matching isolated Unreal PythonScript commandlet."""
import hashlib
import json
import os
from pathlib import Path
import unreal


def main():
    manifest_path = Path(os.environ['CYUEDIFF_REFERENCE_MANIFEST'])
    if not manifest_path.is_absolute():
        raise ValueError('Reference manifest must be an absolute private path')
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    output = Path(manifest['output'])
    if not output.is_absolute() or output.exists():
        raise ValueError('Reference output must be an absolute new directory')
    rows = manifest['assets']
    if not rows or len(rows) > 10000:
        raise ValueError('Select 1..10000 explicit private assets')
    files = set()
    for row in rows:
        name = row['file']
        if not row['object'].startswith('/Game/Corpus/'):
            raise ValueError('Export only explicit /Game/Corpus/ copies')
        if Path(name).name != name or not name.endswith('.json') or name.casefold() in files or name.casefold() == 'export-manifest.json' or ':' in name:
            raise ValueError('Each reference filename must be a unique plain JSON name')
        files.add(name.casefold())
    output.mkdir(parents=True, exist_ok=False)
    evidence = []
    for row in rows:
        asset = unreal.load_asset(row['object'])
        if asset is None:
            raise ValueError('Cannot load ' + row['object'])
        text = unreal.CyUEDiffReferenceLibrary.export_reference(asset)
        data = json.loads(text)
        if data['schema'] != 'cyuediff.unreal-reference/1':
            raise ValueError('Unexpected adapter schema')
        target = output / row['file']
        target.write_text(json.dumps(data, indent=2), encoding='utf-8')
        evidence.append({'object': data['object'], 'file': row['file'],
                         'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    (output / 'export-manifest.json').write_text(json.dumps({'references': evidence}, indent=2), encoding='utf-8')


main()
