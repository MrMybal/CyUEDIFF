"""Compare an installed Core-only source reader with actual Engine-reloaded original hybrid references."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess


def need(ok, message):
    if not ok:
        raise ValueError(message)


def validate(projection, reference, export, reference_object):
    need(reference.get('schema') == 'cyuediff.original-tagged-hybrid-reference/1', 'Unexpected independent reference schema')
    need(reference.get('object') == reference_object, 'Independent object identity differs')
    need(reference.get('engine', '').startswith('5.3.2-'), 'This independent reference targets the selected 5.3.2 producer')
    for key, expected in (('ue4Version', 522), ('ue5Version', 1009), ('licenseeVersion', 0)):
        need(type(projection['summary'][key]) is int and projection['summary'][key] == expected, 'Unexpected selected source archive profile')
    owner = projection['snapshot']['objects'][export]
    need(owner['class'] == '/Script/CyUEDiffReference.CyUEDiffReferenceHybridAsset', 'Unexpected source class')
    properties = owner['properties']
    stats = {'values': 0, 'hybrids': 0, 'int64Values': 0}

    def integer(value, expected):
        need(type(value) is int and type(expected) is int and value == expected, 'Stored integer differs')
        stats['values'] += 1

    def string(value, expected):
        need(type(value) is str and type(expected) is str and value == expected, 'Stored string/name differs')
        stats['values'] += 1

    def hybrid(value, target):
        need(type(value) is dict and value.get('prefix') == 'tagged-properties', 'Missing certified tagged prefix')
        need(value.get('decoder') == 'original.Hybrid' + target + '/1', 'Unexpected original serializer identity')
        data = value['data']
        need(type(data) is dict and set(data) == {'properties', 'native'}, 'Partial hybrid shape')
        stats['hybrids'] += 1
        return data['properties'], data['native']

    def field(fields, name, kind):
        entry = fields[name]
        need(entry['type']['name'] == kind and 'value' in entry and 'opaque' not in entry, 'Unexpected reflected field contract')
        return entry['value']

    def inner(value, expected):
        fields, native = hybrid(value, 'Inner')
        need(set(fields) == {'Marker'} and set(native) == {'stamp', 'label'}, 'Unexpected original inner fields')
        integer(field(fields, 'Marker', 'IntProperty'), expected['marker'])
        decimal = expected['stampDecimal']
        need(type(decimal) is str and decimal == str(int(decimal)) and -(1 << 63) <= int(decimal) < (1 << 63), 'Invalid exact int64 API reference')
        integer(native['stamp'], int(decimal))
        stats['int64Values'] += 1
        string(native['label'], expected['nativeLabel'])

    def outer(value, expected):
        fields, native = hybrid(value, 'Outer')
        need(set(fields) == {'Mode', 'Label', 'Nested', 'Records', 'Labels'} and set(native) == {'samples', 'name', 'enabled'}, 'Unexpected original outer fields')
        integer(field(fields, 'Mode', 'IntProperty'), expected['mode'])
        string(field(fields, 'Label', 'StrProperty'), expected['label'])
        inner(field(fields, 'Nested', 'StructProperty'), expected['nested'])
        records = field(fields, 'Records', 'ArrayProperty')
        need(type(records) is list and len(records) == len(expected['records']), 'Repeated hybrid count differs')
        for stored, actual in zip(records, expected['records']):
            inner(stored, actual)
        labels = field(fields, 'Labels', 'MapProperty')
        need(labels['kind'] == 'stored-map-delta' and labels['removed'] == {}, 'Unexpected original stored map delta')
        stored_labels = {}
        for entry in labels['entries'].values():
            key = entry['key']
            need(type(key) is str and key not in stored_labels, 'Invalid stored map identity')
            stored_labels[key] = entry['value']
        need(stored_labels.keys() == expected['labels'].keys(), 'Original explicit map inventory differs')
        for key, actual in expected['labels'].items():
            string(stored_labels[key], actual)
        samples = native['samples']
        need(type(samples) is list and len(samples) == len(expected['samples']), 'Native sample count differs')
        for stored, actual in zip(samples, expected['samples']):
            need(type(stored) in (int, float) and type(actual) in (int, float) and math.isfinite(stored) and math.isfinite(actual), 'Invalid native float')
            need(struct.unpack('<f', struct.pack('<f', stored))[0] == stored == actual, 'Exact stored float32 differs')
            stats['values'] += 1
        string(native['name'], expected['name'])
        need(type(native['enabled']) is bool and type(expected['enabled']) is bool and native['enabled'] == expected['enabled'], 'Native boolean differs')
        stats['values'] += 1

    outer(field(properties, 'Primary', 'StructProperty'), reference['primary'])
    repeated = field(properties, 'Repeated', 'ArrayProperty')
    need(type(repeated) is list and len(repeated) == len(reference['repeated']), 'Root repeated hybrid count differs')
    for value, expected in zip(repeated, reference['repeated']):
        outer(value, expected)
    integer(field(properties, 'AfterHybrid', 'IntProperty'), reference['afterHybrid'])
    return stats


def reference_values(reference, export):
    """Map the independent original API's named values to the declared source contract."""
    out = {}
    def token(value):
        return value.replace('~', '~0').replace('/', '~1')
    def inner(path, value):
        out[path + '/data/properties/Marker/value'] = value['marker']
        out[path + '/data/native/stamp'] = int(value['stampDecimal'])
        out[path + '/data/native/label'] = value['nativeLabel']
    def outer(path, value):
        out[path + '/data/properties/Mode/value'] = value['mode']
        out[path + '/data/properties/Label/value'] = value['label']
        inner(path + '/data/properties/Nested/value', value['nested'])
        for i, record in enumerate(value['records']):
            inner(path + '/data/properties/Records/value/' + str(i), record)
        for key, label in value['labels'].items():
            identity = token(json.dumps(key, ensure_ascii=False, separators=(',', ':')))
            out[path + '/data/properties/Labels/value/entries/' + identity + '/value'] = label
        for i, sample in enumerate(value['samples']):
            out[path + '/data/native/samples/' + str(i)] = sample
        out[path + '/data/native/name'] = value['name']
        out[path + '/data/native/enabled'] = value['enabled']
    base = '/objects/' + token(export) + '/properties/'
    outer(base + 'Primary/value', reference['primary'])
    for i, value in enumerate(reference['repeated']):
        outer(base + 'Repeated/value/' + str(i), value)
    out[base + 'AfterHybrid/value'] = reference['afterHybrid']
    return out


def validate_comparison(comparison, before_reference, after_reference, export):
    a = reference_values(before_reference, export)
    b = reference_values(after_reference, export)
    need(a.keys() == b.keys(), 'This original comparison expects the same explicit field inventory')
    expected = {p: (a[p], b[p]) for p in a if a[p] != b[p]}
    prefix = '/objects/' + export.replace('~', '~0').replace('/', '~1') + '/properties/'
    rows = [x for x in comparison['changes'] if x['path'].startswith(prefix)]
    need(all(x['op'] == 'replace' and x['path'] in expected and x['before'] == expected[x['path']][0] and x['after'] == expected[x['path']][1] for x in rows), 'Unexpected named source difference')
    need(len(rows) == len(expected) and {x['path'] for x in rows} == expected.keys(), 'Missing or duplicate named source differences')
    return len(rows)


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--manifest', required=True, type=Path)
    parser.add_argument('--consumer', required=True, type=Path)
    parser.add_argument('--output-dir', required=True, type=Path)
    args = parser.parse_args()
    manifest_path = args.manifest.resolve(strict=True)
    consumer = args.consumer.resolve(strict=True)
    manifest = json.loads(manifest_path.read_text(encoding='utf-8'))
    need(manifest.get('schema') == 'cyuediff.tagged-hybrid-validation/1', 'Unexpected validation manifest')
    need(len(manifest['cases']) == 2, 'Select the explicit before/after source pair')
    protected = {manifest_path, consumer, Path(__file__).resolve()}
    cases = []
    for row in manifest['cases']:
        paths = {key: Path(row[key]).resolve(strict=True) for key in ('asset', 'reference', 'layouts')}
        protected.update(paths.values())
        for extension in ('.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload'):
            sibling = paths['asset'].with_suffix(extension)
            if sibling.is_file():
                protected.add(sibling.resolve(strict=True))
        cases.append((row, paths))
    out = args.output_dir.resolve()
    need(not out.exists(), 'Use a new validation output directory')
    need(all(out != p and out not in p.parents for p in protected), 'Output overlaps a protected input')
    hashes = {str(p): digest(p) for p in protected}
    out.mkdir(parents=True, exist_ok=False)
    summaries = []
    for index, (row, paths) in enumerate(cases):
        result = subprocess.run([str(consumer), 'package', str(paths['layouts']), str(paths['asset'])], capture_output=True, timeout=90)
        need(result.returncode == 0, 'Installed source consumer failed: ' + result.stderr.decode('utf-8', 'replace'))
        decoded = json.loads(result.stdout)['before']
        reference = json.loads(paths['reference'].read_text(encoding='utf-8'))
        summary = validate(decoded, reference, row['export'], row['referenceObject'])
        (out / f'{index:03d}.json').write_bytes(result.stdout)
        summaries.append(summary)
    before, after = cases
    result = subprocess.run([str(consumer), 'package', str(before[1]['layouts']), str(before[1]['asset']), str(after[1]['layouts']), str(after[1]['asset'])], capture_output=True, timeout=90)
    need(result.returncode == 0, 'Installed source comparison failed')
    comparison = json.loads(result.stdout)['comparison']
    need(before[0]['export'] == after[0]['export'], 'Select the original matching root export pair')
    named = validate_comparison(comparison, json.loads(before[1]['reference'].read_text(encoding='utf-8')), json.loads(after[1]['reference'].read_text(encoding='utf-8')), before[0]['export'])
    (out / 'comparison.json').write_bytes(result.stdout)
    need(all(digest(Path(p)) == h for p, h in hashes.items()), 'A protected input changed')
    summary = {'schema': 'cyuediff.tagged-hybrid-validation/1', 'cases': summaries, 'values': sum(x['values'] for x in summaries), 'hybrids': sum(x['hybrids'] for x in summaries), 'int64Values': sum(x['int64Values'] for x in summaries), 'namedChanges': named, 'snapshotChanges': len(comparison['changes']), 'inputSha256': hashes, 'inputsUnchanged': True}
    (out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8', newline='\n')
    print(json.dumps({k: v for k, v in summary.items() if k not in ('inputSha256', 'cases')}))


if __name__ == '__main__':
    main()
