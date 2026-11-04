"""History selection to native asset comparison, catalogue fallback and isolated failures."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
exe, cli, fixture, git = [str(Path(p).resolve()) for p in sys.argv[1:]]
env = {key.upper() if os.name == 'nt' else key: value for key,value in os.environ.items()
       if not key.upper().startswith('GIT_')}
checks = 0
with tempfile.TemporaryDirectory(prefix='.test-tmp-history-desktop-',dir=Path.cwd()) as tmp:
    root = Path(tmp); repo = root/'répo [history]'; repo.mkdir()
    env['LOCALAPPDATA'] = str(root/'profile')
    fixtures = root/'fixtures'
    subprocess.run([fixture,'--write-fixtures',str(fixtures)],env=env,check=True,capture_output=True,timeout=20)
    def command(*args):
        return subprocess.run([git,'-C',str(repo),*args],env=env,check=True,capture_output=True,timeout=30).stdout.decode('utf-8').strip()
    command('init','-q')
    for key,value in [('user.name','Auteur é'),('user.email','test@invalid.example'),('commit.gpgSign','false'),('core.hooksPath','missing-hooks')]:
        command('config',key,value)
    (repo/'Content').mkdir(); asset=repo/'Content'/'Asset.uasset'
    shutil.copyfile(fixtures/'before'/'Asset.uasset',asset)
    command('add','Content');command('commit','-q','-m','Before é')
    before=command('rev-parse','HEAD')
    shutil.copyfile(fixtures/'after'/'Asset.uasset',asset)
    command('add','Content');command('commit','-q','-m','After ### 100%')
    after=command('rev-parse','HEAD')
    command('commit','-q','--allow-empty','-m','Current tip');tip=command('rev-parse','HEAD')
    baseline={p.relative_to(repo).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in repo.rglob('*') if p.is_file()}
    def run(*args,code=0):
        global checks
        report=root/'report.json';report.unlink(missing_ok=True)
        p=subprocess.run([exe,'--smoke','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
        assert p.returncode==code,p.stderr.decode('utf-8','replace')
        checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    base=['--repo',repo,'--history']
    report=run(*base,'--locale','fr')
    assert report['history']['head']==tip and report['history']['visibleCommits']==3 and report['history']['open']
    assert report['document'] is None and report['locale']=='fr'
    checks+=1
    report=run(*base,'--history-filter','auteur')
    assert report['history']['visibleCommits']==3
    report=run(*base,'--history-filter','no matching text')
    assert report['history']['visibleCommits']==0 and report['history']['selected'] is None
    checks+=1
    selection=['--history-before',before,'--history-after',after,'--history-compare']
    report=run(*base,*selection,'--before','Content/Asset.uasset','--view','comparison','--no-previews')
    assert report['document']['status']=='changed' and not report['history']['open']
    assert report['document']['git']['before']['commit']==before and report['document']['git']['after']['commit']==after
    expected=subprocess.run([cli,'git-diff',str(repo),before,after,'Content/Asset.uasset'],env=env,capture_output=True,timeout=30)
    assert expected.returncode == 1, expected.stderr.decode('utf-8','replace')
    assert report['document']['diff']['changes']==json.loads(expected.stdout)['comparison']['changes']
    checks+=1
    report=run(*base,*selection)
    assert report['browser']['open'] and report['browser']['before']['revision']==before and report['browser']['after']['revision']==after
    checks+=1
    report=run(*base,'--history-before',before,'--history-after',before,'--history-compare','--before','Content/Asset.uasset')
    assert report['document']['status']=='identical'
    checks+=1
    report=run(*base,'--history-before','missing',code=2)
    assert 'Requested commit' in report['error'] and report['document'] is None
    checks+=1
    report=run(*base,'--history-revision','missing-revision',code=2)
    assert report['history'] is None
    checks+=1
    run(*base,'--history-compare',code=2)
    run(*base,'--browse',code=2)
    run('--history',code=2)
    run('--history-filter','text',code=2)
    protected=repo/'.git'/'history.json'
    p=subprocess.run([exe,'--smoke',*map(str,base),'--report',str(protected)],env=env,capture_output=True,timeout=40)
    assert p.returncode==2 and not protected.exists()
    checks+=1
    assert baseline=={p.relative_to(repo).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in repo.rglob('*') if p.is_file()}
    assert not (root/'profile').exists()
    checks+=1
print(f'{checks} desktop history checks passed; source and Git storage hashes unchanged')
