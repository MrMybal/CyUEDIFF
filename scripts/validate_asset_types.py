"""Audit all mounted project packages and optional loose assets without changing inputs.

Opening, property/native coverage and specialized records are reported separately.
Stored export classes are inventoried without a built-in list of allowed asset types.
"""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def digest(path):
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cli', type=Path, required=True)
    parser.add_argument('--project', type=Path)
    parser.add_argument('--engine-root', type=Path)
    parser.add_argument('--include-engine', action='store_true')
    parser.add_argument('--asset', type=Path, action='append', default=[])
    parser.add_argument('--output-dir', type=Path, required=True)
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=4)
    parser.add_argument('--timeout', type=int, default=120)
    args = parser.parse_args()
    if not args.project and not args.asset:
        parser.error('Provide --project or at least one --asset')
    if (args.engine_root or args.include_engine) and not args.project:
        parser.error('--engine-root and --include-engine require --project')
    if args.timeout < 1:
        parser.error('--timeout must be positive')
    cli = args.cli.resolve(strict=True)
    project = args.project.resolve(strict=True) if args.project else None
    output = args.output_dir.resolve()
    assets = {path.resolve(strict=True) for path in args.asset}
    context = ['--project', str(project)] if project else []
    if args.engine_root:
        context += ['--engine-root', str(args.engine_root.resolve(strict=True))]
    env = {key.upper(): value for key, value in os.environ.items()}
    catalog = None
    if project:
        command = [str(cli), 'catalog', str(project), *context[2:]]
        if args.include_engine:
            command.append('--include-engine')
        process = subprocess.run(command, env=env, capture_output=True, timeout=args.timeout)
        if process.returncode:
            raise RuntimeError(process.stderr.decode('utf-8', 'replace') or 'Incomplete catalogue')
        catalog = json.loads(process.stdout)
        if not catalog['complete']:
            raise RuntimeError('Incomplete catalogue: no whole-project claim can be made')
        assets.update(Path(item['path']).resolve(strict=True) for item in catalog['assets'])
    protected_roots = [project.parent] if project else []
    if args.engine_root:
        protected_roots.append(args.engine_root.resolve(strict=True))
    if any(output == root or root in output.parents for root in protected_roots):
        raise ValueError('Choose an output directory outside the input project and engine')
    companions = ('.uexp', '.ubulk', '.uptnl', '.m.ubulk', '.o.ubulk', '.upayload')
    inputs = set(assets)
    if project:
        inputs.add(project)
    for asset in assets:
        if output == asset or asset in output.parents:
            raise ValueError('Output overlaps an input')
        inputs.update(path for suffix in companions if (path := asset.with_suffix(suffix)).is_file())
    if output.exists():
        raise ValueError('Use a fresh output directory to preserve earlier audit evidence')
    output.mkdir(parents=True)
    (output / 'reports').mkdir()
    hashes = {str(path): digest(path) for path in sorted(inputs)}
    (output / 'inputs.json').write_text(json.dumps(hashes, indent=2) + '\n', encoding='utf-8')
    if catalog:
        (output / 'catalog.json').write_text(json.dumps(catalog, indent=2) + '\n', encoding='utf-8')

    def inspect(index, asset):
        row = {'path': str(asset), 'report': f'reports/{index:05d}.json'}
        try:
            process = subprocess.run([str(cli), 'inspect', str(asset), *context], env=env,
                                     capture_output=True, timeout=args.timeout)
            row['exitCode'] = process.returncode
            if process.stdout:
                (output / row['report']).write_bytes(process.stdout)
            if process.stderr:
                (output / 'reports' / f'{index:05d}.stderr.txt').write_bytes(process.stderr)
            data = json.loads(process.stdout) if process.stdout else {}
            row['opened'] = data.get('schema') == 'cyuediff.package/1'
            if not row['opened']:
                row['error'] = process.stderr.decode('utf-8', 'replace')[:4096]
                return row
            objects = data.get('snapshot', {}).get('objects', {})
            row['classes'] = sorted({obj.get('class', '') for obj in objects.values()})
            row['categories'] = sorted({obj.get('category', '') for obj in objects.values()})
            row['objects'] = len(objects)
            row['completeInput'] = data.get('completeInput', False)
            row['sourceReadLimitFailed'] = data.get('sourceReadLimitFailed', False)
            coverage = list(data.get('coverage', {}).values())
            row['coverage'] = dict(Counter(obj.get('coverage', 'unknown') for obj in coverage))
            row['nativeCoverage'] = dict(Counter(obj.get('nativeCoverage', 'unknown') for obj in coverage))
            row['diagnostics'] = data.get('diagnostics', [])
            row['storedProperties'] = sum(len(obj.get('properties', {})) for obj in objects.values())
            row['specializedRecords'] = {
                key: len(data.get(section, {}).get(key, []))
                for section, key in [('blueprint', 'graphs'), ('textures', 'textures'),
                                     ('meshes', 'meshes'), ('materials', 'instances'),
                                     ('skeletons', 'skeletons')]}
        except subprocess.TimeoutExpired:
            row.update(opened=False, error='timeout')
        except (OSError, ValueError, TypeError) as error:
            row.update(opened=False, error=str(error))
        return row

    rows = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(inspect, i, asset) for i, asset in enumerate(sorted(assets))]
        for future in as_completed(futures):
            row = future.result()
            rows.append(row)
            print(f"{len(rows)}/{len(assets)} {'opened' if row['opened'] else 'FAILED'}: "
                  f"{Path(row['path']).name}", flush=True)
    changed = [path for path, old in hashes.items() if not Path(path).is_file() or digest(Path(path)) != old]
    summary = {'schema': 'cyuediff.asset-audit/1', 'cliSha256': digest(cli),
               'packages': len(rows), 'opened': sum(row['opened'] for row in rows),
               'failedCommands': sum(row.get('exitCode', -1) != 0 for row in rows),
               'incompleteInputs': sum(row.get('opened', False) and not row.get('completeInput', False)
                                       for row in rows),
               'limitedReads': sum(row.get('sourceReadLimitFailed', False) for row in rows),
               'packagesWithDiagnostics': sum(bool(row.get('diagnostics')) for row in rows),
               'classes': dict(sorted(Counter(cls for row in rows for cls in row.get('classes', [])).items())),
               'changedInputs': changed, 'assets': sorted(rows, key=lambda row: row['path'])}
    (output / 'summary.json').write_text(json.dumps(summary, indent=2, ensure_ascii=False) + '\n',
                                       encoding='utf-8')
    print(json.dumps({key: value for key, value in summary.items() if key not in ('assets', 'classes')}))
    return int(bool(changed) or summary['opened'] != len(rows) or summary['failedCommands'] > 0
               or summary['incompleteInputs'] > 0
               or summary['limitedReads'] > 0)


if __name__ == '__main__':
    sys.exit(main())
