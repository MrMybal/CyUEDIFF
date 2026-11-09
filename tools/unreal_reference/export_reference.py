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
        if row.get('mode', 'source') not in ('source', 'nanite-source', 'compressed-animation', 'per-track-synthetic', 'compressed-curves', 'compressed-curve-synthetic', 'physical-cloth', 'physical-cloth-geometry'):
            raise ValueError('Unknown reference export mode')
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
        mode = row.get('mode', 'source')
        methods = {
            'physical-cloth-geometry': (unreal.CyUEDiffReferenceLibrary.export_physical_cloth_geometry_reference, 'cyuediff.unreal-physical-cloth-geometry-reference/1'),
            'physical-cloth': (unreal.CyUEDiffReferenceLibrary.export_physical_cloth_reference, 'cyuediff.unreal-physical-cloth-reference/1'),
            'compressed-curves': (unreal.CyUEDiffReferenceLibrary.export_compressed_curves_reference, 'cyuediff.unreal-compressed-curves-reference/1'),
            'compressed-curve-synthetic': (unreal.CyUEDiffReferenceLibrary.export_compressed_curve_synthetic_reference, 'cyuediff.unreal-compressed-curve-synthetic-reference/1'),
            'nanite-source': (unreal.CyUEDiffReferenceLibrary.export_nanite_source_reference, 'cyuediff.unreal-nanite-source-reference/1'),
            'source': (unreal.CyUEDiffReferenceLibrary.export_reference, 'cyuediff.unreal-reference/1'),
            'compressed-animation': (unreal.CyUEDiffReferenceLibrary.export_compressed_animation_reference, 'cyuediff.unreal-compressed-animation-reference/1'),
            'per-track-synthetic': (unreal.CyUEDiffReferenceLibrary.export_per_track_synthetic_reference, 'cyuediff.unreal-per-track-synthetic-reference/1'),
        }
        method, schema = methods[mode]
        text = method(asset)
        data = json.loads(text)
        if data['schema'] != schema:
            raise ValueError('Unexpected adapter schema')
        target = output / row['file']
        with target.open('w', encoding='utf-8', newline='\n') as stream:
            json.dump(data, stream, indent=2); stream.write('\n')
        evidence.append({'object': data.get('object', row['object']), 'mode': mode, 'file': row['file'],
                         'sha256': hashlib.sha256(target.read_bytes()).hexdigest()})
    with (output / 'export-manifest.json').open('w', encoding='utf-8', newline='\n') as stream:
        json.dump({'references': evidence}, stream, indent=2); stream.write('\n')


main()
