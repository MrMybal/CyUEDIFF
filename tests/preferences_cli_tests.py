"""Native preference startup/override/restart tests; isolated from the user's profile."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-preferences-cli-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    env = {key.upper(): value for key, value in os.environ.items()}
    env['LOCALAPPDATA'] = str(root / 'profile')

    def run(*args, code=0):
        global checks
        report = root / 'report.json'
        report.unlink(missing_ok=True)
        result = subprocess.run([exe, '--smoke', '--report', str(report), *map(str, args)],
                                env=env, capture_output=True, timeout=40)
        assert result.returncode == code, result.stderr.decode('utf-8', 'replace')
        checks += 1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None

    report = run()
    assert report['locale'] == 'en' and report['theme'] == 'mid-grey'
    assert report['preferences']['enabled'] is False and not (root / 'profile').exists()
    checks += 1
    prefs = root / 'préférences.json'
    data = {'schema': 'cyuediff.preferences/1', 'theme': 'light-grey', 'locale': 'fr',
            'previews': False, 'includeEngine': True}
    prefs.write_text(json.dumps(data), encoding='utf-8')
    original = prefs.read_bytes()
    report = run('--preferences', prefs)
    assert report['theme'] == 'light-grey' and report['locale'] == 'fr'
    assert report['preferences']['values'] == data and report['preferences']['loaded']
    assert not report['preferences']['saved'] and prefs.read_bytes() == original
    checks += 1
    report = run('--preferences', prefs, '--locale', 'en', '--theme', 'violet', '--previews', '--exclude-engine')
    values = report['preferences']['values']
    assert values['theme'] == 'violet' and values['locale'] == 'en' and values['previews'] and not values['includeEngine']
    assert prefs.read_bytes() == original
    checks += 1
    prefs.unlink()
    report = run('--preferences', prefs, '--save-preferences', '--theme', 'navy', '--locale', 'fr', '--no-previews')
    assert report['preferences']['saved'] and not report['preferences']['loaded']
    saved = json.loads(prefs.read_text(encoding='utf-8'))
    report = run('--preferences', prefs)
    assert report['preferences']['loaded'] and report['preferences']['values'] == saved
    assert set(saved) == {'schema', 'theme', 'locale', 'previews', 'includeEngine'}
    checks += 1
    # Malformed/future/foreign files retain exact bytes, even with an explicit save request.
    for text in ['{', '{"schema":"future"}', '{"theme":"navy","theme":"violet"}']:
        prefs.write_text(text, encoding='utf-8')
        report = run('--preferences', prefs, '--save-preferences')
        assert report['status'] == 'ready' and report['preferences']['diagnostic']
        assert not report['preferences']['saved'] and prefs.read_text(encoding='utf-8') == text
        checks += 1
    run('--preferences', root / 'report.json', code=2)
    prefs.write_text(json.dumps(data), encoding='utf-8')
    alias = root / 'alias.json'
    os.link(prefs, alias)
    # A report must not replace preferences through a hard link either.
    result = subprocess.run([exe, '--smoke', '--preferences', str(prefs), '--report', str(alias)],
                            env=env, capture_output=True, timeout=40)
    assert result.returncode == 2 and json.loads(prefs.read_text()) == data
    checks += 1
    asset = root / 'source.uasset'
    os.link(prefs, asset)
    # Even a schema-shaped input cannot be replaced through a preference alias.
    original = asset.read_bytes()
    run('--preferences', prefs, '--save-preferences', '--before', asset, code=2)
    assert asset.read_bytes() == original and prefs.read_bytes() == original
    checks += 1
    run('--no-preferences', '--preferences', prefs, code=2)
    run('--save-preferences', code=2)
    run('--preferences', root / 'file.uasset', code=2)
    report = run('--no-preferences')
    assert not report['preferences']['enabled'] and not (root / 'profile').exists()
    assert not list(root.glob('*.tmp-*'))
    checks += 1
print(f'{checks} native preferences checks passed')
