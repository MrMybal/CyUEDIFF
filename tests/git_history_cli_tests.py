"""History protocol, pinned pagination and read-only CLI integration."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
cli, git = map(str, map(Path, sys.argv[1:3]))
cli = str(Path(cli).resolve())
env = {key.upper() if os.name == 'nt' else key: value for key, value in os.environ.items()}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-history-cli-', dir=Path.cwd()) as tmp:
    root = Path(tmp)
    repo = root / 'répo [local]'; repo.mkdir()
    def command(*args):
        return subprocess.run([git, '-C', str(repo), *args], check=True, env=env,
                              capture_output=True, timeout=30).stdout.decode('utf-8').strip()
    command('init', '-q')
    for key, value in [('user.name','Auteur é'), ('user.email','test@invalid.example'),
                       ('commit.gpgSign','false'), ('core.hooksPath','missing-hooks')]:
        command('config', key, value)
    command('commit','-q','--allow-empty','-m','Initial é')
    initial = command('rev-parse','HEAD')
    command('branch','feature')
    command('commit','-q','--allow-empty','-m','Second ### 100%')
    second = command('rev-parse','HEAD')
    main = command('branch','--show-current')
    command('checkout','-q','feature')
    command('commit','-q','--allow-empty','-m','Feature')
    feature = command('rev-parse','HEAD')
    command('checkout','-q',main)
    command('merge','--no-ff','-m','Merge é','feature')
    tip = command('rev-parse','HEAD')
    baseline = {p.relative_to(repo).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                for p in repo.rglob('*') if p.is_file()}
    def run(*args, code=0):
        global checks
        result = subprocess.run([cli, *map(str,args)],env=env,capture_output=True,timeout=30)
        assert result.returncode == code, result.stderr.decode('utf-8','replace')
        checks += 1
        return json.loads(result.stderr if code else result.stdout) if (result.stderr if code else result.stdout) else None
    page = run('git-history',repo,'HEAD','--max-commits','1')
    assert page['head'] == tip and page['hasMore'] and page['nextSkip'] == 1
    assert page['commits'][0]['parents'] == [second,feature]
    assert page['commits'][0]['author'] == 'Auteur é' and page['commits'][0]['subject'] == 'Merge é'
    checks += 1
    remaining = run('git-history',repo,page['head'],'--skip','1','--max-commits','1000')
    assert {r['id'] for r in remaining['commits']} == {second, feature, initial}
    assert not remaining['hasMore'] and remaining['nextSkip'] == 4
    checks += 1
    empty = run('git-history',repo,tip,'--skip','4')
    assert not empty['commits'] and not empty['hasMore']
    checks += 1
    report = root / 'history.json'
    run('git-history',repo,tip,'--out',report)
    assert json.loads(report.read_text())['head'] == tip
    checks += 1
    for extra in [['--max-commits','0'], ['--max-commits','1001'], ['--skip','1000001'],
                  ['--skip','-1'], ['--metadata'], ['--no-engine']]:
        run('git-history',repo,tip,*extra,code=2)
    run('git-history',repo,'--all',code=2)
    run('git-catalog',repo,tip,'--skip','1',code=2)
    run('git-history',repo,tip,'--out',repo/'.git'/'history.json',code=2)
    assert not (repo/'.git'/'history.json').exists()
    after = {p.relative_to(repo).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
             for p in repo.rglob('*') if p.is_file()}
    assert baseline == after
    checks += 1
    # Git's SHA-256 object format uses full 64-character IDs and parents.
    sha_repo = root / 'sha256'; sha_repo.mkdir()
    subprocess.run([git,'-C',str(sha_repo),'init','-q','--object-format=sha256'],env=env,check=True,capture_output=True)
    for key,value in [('user.name','Test'),('user.email','test@invalid.example'),('commit.gpgSign','false'),('core.hooksPath','missing-hooks')]:
        subprocess.run([git,'-C',str(sha_repo),'config',key,value],env=env,check=True,capture_output=True)
    subprocess.run([git,'-C',str(sha_repo),'commit','-q','--allow-empty','-m','Root'],env=env,check=True,capture_output=True)
    result = run('git-history',sha_repo,'HEAD')
    assert len(result['head']) == 64 and result['commits'][0]['id'] == result['head']
    checks += 1
print(f'{checks} history CLI checks passed; repository bytes unchanged')
