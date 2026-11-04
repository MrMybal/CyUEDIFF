"""Scan publishable working-tree files and Git history with Gitleaks; keep redacted reports local."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile


def git(root, *arguments):
    result = subprocess.run(['git', '-C', str(root), *arguments], capture_output=True, check=True)
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gitleaks', type=Path, help='Gitleaks executable; otherwise use PATH or the Windows user installation')
    parser.add_argument('--report-dir', type=Path, default=Path('build-native/security'))
    parser.add_argument('--working-tree-only', action='store_true', help='Run before the first commit; explicitly skip history')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    if Path(os.fsdecode(git(root, 'rev-parse', '--show-toplevel')).strip()).resolve() != root:
        parser.error('Run against an initialized Git repository rooted at this project')
    executable = str(args.gitleaks.resolve()) if args.gitleaks else shutil.which('gitleaks')
    if not executable and os.name == 'nt':
        candidate = Path(os.environ.get('LOCALAPPDATA', '')) / 'Programs/gitleaks/gitleaks.exe'
        if candidate.is_file():
            executable = str(candidate)
    if not executable:
        parser.error('Gitleaks was not found; provide --gitleaks or add it to PATH')
    report_dir = args.report_dir.resolve()
    try:
        relative = report_dir.relative_to(root)
    except ValueError:
        parser.error('Reports must stay inside this workspace in an ignored build directory')
    if not relative.parts or not relative.parts[0].startswith('build'):
        parser.error('Use an ignored top-level build directory for reports')
    report_dir.mkdir(parents=True, exist_ok=True)
    summary_path = report_dir / 'summary.json'
    if summary_path.exists():
        summary_path.unlink()
    ignored = subprocess.run(['git', '-C', str(root), 'check-ignore', '-q', str(report_dir)])
    if ignored.returncode != 0:
        parser.error('The report directory must be ignored by Git')
    version = subprocess.run([executable, 'version'], check=True, capture_output=True, text=True).stdout.strip()
    manifest = []
    paths = sorted(set(git(root, 'ls-files', '--cached', '--others', '--exclude-standard', '-z').split(b'\0')) - {b''})
    common = ['--config', str(root / '.gitleaks.toml'), '--redact=100', '--no-banner', '--no-color',
              '--ignore-gitleaks-allow', '--report-format', 'json']

    def scan(mode, source, name, extra=()):
        report = report_dir / name
        if report.exists():
            report.unlink()  # Never accept a stale successful report after a scanner error.
        result = subprocess.run([executable, mode, str(source), *common, '--report-path', str(report),
                                 '--gitleaks-ignore-path', str(report_dir / 'no-ignore-file'), *extra],
                                cwd=root, capture_output=True, text=True, encoding='utf-8')
        findings = json.loads(report.read_text(encoding='utf-8-sig')) if report.is_file() else None
        if findings:
            for item in findings:
                print(f"{item.get('RuleID')}: {item.get('File')}:{item.get('StartLine')}")
        if result.returncode or findings is None or findings:
            print(result.stderr, file=sys.stderr)
            raise RuntimeError(f'Gitleaks {mode} failed or reported findings (exit {result.returncode})')
        print(f'Gitleaks {mode}: 0 findings')
        return {'exitCode': result.returncode, 'findings': len(findings), 'report': name}

    with tempfile.TemporaryDirectory(prefix='source-scan-', dir=report_dir) as directory:
        stage = Path(directory)
        for encoded in paths:
            path = Path(encoded.decode('utf-8'))
            if path.is_absolute() or '..' in path.parts:
                raise RuntimeError('Non-relative Git source path')
            source = root / path
            if source.is_symlink():
                raise RuntimeError('Review symbolic links explicitly before publishing')
            if not source.exists():
                continue
            if not source.is_file():
                raise RuntimeError('Review submodules or non-regular source entries explicitly')
            destination = stage / path
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, destination)
            data = destination.read_bytes()
            manifest.append({'path': path.as_posix(), 'bytes': len(data), 'sha256': hashlib.sha256(data).hexdigest()})
        working_tree = scan('dir', stage, 'working-tree.json')
    history = None
    if not args.working_tree_only:
        git(root, 'rev-parse', '--verify', 'HEAD')
        history = scan('git', root, 'history.json', ['--log-opts=--all'])
    manifest_text = json.dumps(manifest, ensure_ascii=False, sort_keys=True, indent=2) + '\n'
    (report_dir / 'manifest.json').write_text(manifest_text, encoding='utf-8')
    summary = {'schema': 'cyuediff.secret-scan/1', 'gitleaksVersion': version,
               'sourceFiles': len(manifest), 'sourceBytes': sum(item['bytes'] for item in manifest),
               'manifestSha256': hashlib.sha256(manifest_text.encode('utf-8')).hexdigest(),
               'workingTree': working_tree, 'history': history,
               'scope': 'Tracked working-tree files and non-ignored untracked files; all refs for history'}
    (report_dir / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n', encoding='utf-8')
    print(f'{len(manifest)} publishable source files scanned with Gitleaks {version}; reports: {report_dir}')
    if args.working_tree_only:
        print('History was explicitly skipped. Run again without --working-tree-only after committing.')


if __name__ == '__main__':
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError, OSError, ValueError) as error:
        print(f'Secret scan failed: {error}', file=sys.stderr)
        sys.exit(1)
