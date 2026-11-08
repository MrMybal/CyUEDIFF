"""CLI contract and source protection checks using original synthetic containers."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

cli, fixtures = map(lambda p: str(Path(p).resolve()), sys.argv[1:])
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-container-cli-', dir=Path.cwd()) as temp:
    root = Path(temp)
    subprocess.run([fixtures, '--write-fixtures', temp], check=True, timeout=20)
    originals = list(root.iterdir())
    fingerprints = {p: hashlib.sha256(p.read_bytes()).hexdigest() for p in originals}

    def run(*args, code=0, error=None):
        global checks
        result = subprocess.run([cli, *map(str, args)], capture_output=True, timeout=20)
        assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
        report = json.loads(result.stderr if code == 2 else result.stdout)
        if error:
            assert report['error'] == error, report
        checks += 1
        return report

    pak, toc = root/'plain.pak', root/'test.utoc'
    for path, key, data in [(pak, 'Content/A.bin', b'abcdef'), (toc, 'Content/A.bin', b'bcdef')]:
        assert run('container', path)['coverage'] == 'container-bytes'
        assert run('container-hash', path)['decodedSha256']
        destination = root/(path.suffix+'.bin')
        report = run('container-read', path, key, '--out', destination)
        assert destination.read_bytes() == data
        assert report['sha256'] == hashlib.sha256(data).hexdigest()
        assert run('container-diff', path, path)['status'] == 'identical'
        run('container-read', path, key, error='arguments', code=2)
        run('container-read', path, 'missing', '--out', destination, error='container-entry', code=2)
        run('container', path, '--snapshot', error='arguments', code=2)
        run('container', path, '--codec', 'ignored.dll', error='arguments', code=2)
        run('container-read', path, key, '--mip', '0', error='arguments', code=2)
        for source in ([pak] if path == pak else [toc, root/'test.ucas', root/'test_s1.ucas']):
            run('container', path, '--out', source, error='output-path', code=2)
            run('container-read', path, key, '--out', source, error='output-path', code=2)
            hardlink = root/'hardlink.bin'
            os.link(source, hardlink)
            try:
                run('container-read', path, key, '--out', hardlink, error='output-path', code=2)
            finally:
                hardlink.unlink()
    # Same index and sizes with corrupted source bytes must fail integrity checks.
    other = root/'other.utoc'
    other.write_bytes(toc.read_bytes())
    (root/'other.ucas').write_bytes(b'abXd')
    (root/'other_s1.ucas').write_bytes(b'efgh')
    run('container-diff', toc, other, code=2, error='container-integrity')
    corrupt_out = root/'corrupt-output.bin'
    run('container-read', other, 'Content/A.bin', '--out', corrupt_out, code=2, error='container-integrity')
    assert not corrupt_out.exists()
    corrupt_out.write_bytes(b'preserve existing output')
    run('container-read', other, 'Content/A.bin', '--out', corrupt_out, code=2, error='container-integrity')
    assert corrupt_out.read_bytes() == b'preserve existing output'
    diff = run('container-diff', toc, root/'changed.utoc', code=1)
    assert len(diff['changes']) == 2 and all(c['change'] == 'modified' for c in diff['changes'])
    run('container-read', pak, 'Content/Gone.bin', '--out', root/'gone.bin', code=2, error='container-deleted')
    assert not (root/'gone.bin').exists()
    assert all(hashlib.sha256(p.read_bytes()).hexdigest() == sha for p, sha in fingerprints.items())
print(f'{checks} container CLI checks passed; original sources unchanged')
