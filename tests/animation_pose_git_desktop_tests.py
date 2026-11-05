"""Native per-revision Skeleton bindings and persisted Git source-pose review."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,generator,git_exe=(str(Path(x).resolve()) for x in sys.argv[1:4]);checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-animation-git-cli-',dir=Path.cwd()) as tmp:
 root=Path(tmp);repo=root/'repo';repo.mkdir();fixtures=root/'fixtures';output=root/'out';output.mkdir()
 env['LOCALAPPDATA']=str(root/'unused-profile')
 env.update(GIT_CONFIG_COUNT='1',GIT_CONFIG_KEY_0='safe.directory',GIT_CONFIG_VALUE_0=str(repo).replace('\\','/'))
 subprocess.run([generator,'--write-fixtures',str(fixtures)],check=True,env=env,stdout=subprocess.DEVNULL)
 def git(*args):return subprocess.check_output([git_exe,'-C',str(repo),*args],env=env).decode().strip()
 git('init','--quiet')
 for k,v in [('user.name','CyUEDiff tests'),('user.email','tests@invalid.example'),('core.autocrlf','false'),('core.hooksPath','nonexistent-hooks'),('commit.gpgSign','false')]:git('config',k,v)
 def commit(subject):
  git('add','Content','Game.uproject');git('commit','--quiet','--allow-empty','-m',subject);return git('rev-parse','HEAD')
 (repo/'Content').mkdir();(repo/'Game.uproject').write_text('{"FileVersion":3}',encoding='utf-8')
 empty=commit('empty')
 (repo/'Content/Clip.uasset').write_bytes((fixtures/'animation-0-1013.uasset').read_bytes())
 def rig(variant):
  data=(fixtures/f'skeleton-{variant}.uasset').read_bytes();oid=hashlib.sha256(data).hexdigest()
  target=repo/'.git/lfs/objects'/oid[:2]/oid[2:4]/oid;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(data)
  (repo/'Content/Rig.uasset').write_text(f'version https://git-lfs.github.com/spec/v1\noid sha256:{oid}\nsize {len(data)}\n',encoding='utf-8',newline='\n');return oid
 before_oid=rig(0);before=commit('initial hierarchy');rig(2);after=commit('reparented hierarchy')
 (repo/'Content/Rig.uasset').write_bytes(b'invalid checkout Skeleton')
 (repo/'Game.uproject').write_text('invalid checkout descriptor',encoding='utf-8')
 originals={p:p.read_bytes() for p in [repo/'Content/Rig.uasset',repo/'Game.uproject',repo/'.git/index']};status=git('status','--porcelain=v1')
 report=output/'report.json';workspace=output/'workspace.json'
 def run(*args,code=0):
  global checks
  report.unlink(missing_ok=True)
  p=subprocess.run([exe,'--smoke','--no-preferences','--no-previews','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=60)
  assert p.returncode==code,(args,p.returncode,p.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
 def pose(data):return data['document']['animationPose']
 base=['--repo',repo,'--before-revision',before,'--after-revision',after,'--before','Content/Clip.uasset','--pose-before-skeleton','Content/Rig.uasset','--pose-after-skeleton','Content/Rig.uasset','--pose-frame','1','--view','animations']
 d=run(*base,'--save-workspace',workspace);p=pose(d)
 assert p['complete'] and p['comparison']['changedJoints']==1 and p['beforeBinding']['commit']==before and p['afterBinding']['commit']==after
 assert p['beforeBinding']['revision']['files'][0]['lfsOid']==before_oid
 saved=json.loads(workspace.read_text(encoding='utf-8'));s=saved['documents'][0]['settings'];assert s['source']==1 and s['pose_enabled'] and s['pose_before_skeleton']=='Content/Rig.uasset'
 state={'asset':{'animationPose':{'selected':'arm~/track','positionTolerance':.5},'animationPosePreview':True}}
 d=run('--workspace',workspace,'--document-action','view-state:'+json.dumps(state),'--document-action','duplicate');assert len(d['workspace']['documents'])==2 and pose(d)['complete']
 d=run('--workspace',workspace,'--document-action','reload');assert pose(d)['complete'] and d['workspace']['documents'][0]['revision']==2
 d=run('--workspace',workspace,'--document-action','close','--document-action','reopen');assert pose(d)['complete']
 mounted=[*base,'--before','/Game/Clip','--project','Game.uproject','--no-engine','--pose-before-skeleton','/Game/Rig','--pose-after-skeleton','/Game/Rig']
 d=run(*mounted);assert pose(d)['complete'] and pose(d)['beforeBinding']['revision']['path']=='Content/Rig.uasset'
 d=run(*base,'--pose-after-skeleton','Content/Missing.uasset');assert pose(d)['status']=='incomplete' and pose(d)['comparison']['joints']['arm~/track']['kind']=='unavailable'
 run('--repo',repo,'--before','Content/Clip.uasset','--pose-before-skeleton','Content/Rig.uasset',code=2)
 run(*base,'--pose-after-frame','2',code=2)
 d=run(*base[:-4], '--view','animations','--pose-before-frame','0','--pose-after-frame','2');assert pose(d)['complete'] and pose(d)['before']['frame']==0 and pose(d)['after']['frame']==2
 d=run(*base,'--history','--history-before',before,'--history-after',after,'--history-compare');assert pose(d)['complete'] and pose(d)['comparison']['changedJoints']==1
 d=run(*base,'--before-revision',empty);assert pose(d)['complete'] and pose(d)['before'] is None and pose(d)['after'] is not None
 d=run(*base,'--after-revision',empty);assert pose(d)['complete'] and pose(d)['after'] is None and pose(d)['before'] is not None
 d=run(*base,'--pose-before-object','missing');assert not pose(d)['complete'] and pose(d)['after']['complete']
 d=run(*base,'--pose-before-skeleton','../outside.uasset');assert not pose(d)['complete'] and pose(d)['after']['complete']
 d=run(*base,'--pose-before-skeleton',fixtures/'skeleton-0.uasset');assert not pose(d)['complete'] and pose(d)['after']['complete']
 d=run(*mounted,'--pose-after-skeleton','/Unknown/Rig');assert not pose(d)['complete'] and pose(d)['before']['complete']
 lfs=repo/'.git/lfs/objects';external=root/'external-lfs';lfs.rename(external)
 d=run(*base,'--lfs-objects',external);assert pose(d)['complete']
 external.rename(lfs)
 object_file=lfs/before_oid[:2]/before_oid[2:4]/before_oid;original_object=object_file.read_bytes();object_file.unlink()
 d=run(*base);assert not pose(d)['complete'] and pose(d)['after']['complete'] and pose(d)['beforeBinding']['commit']==before
 object_file.write_bytes(b'corrupt local LFS content')
 d=run(*base);assert not pose(d)['complete'] and pose(d)['after']['complete']
 object_file.write_bytes(original_object)
 # Aliases exercise filesystem equivalence guards before output creation, including an inactive Git tab.
 alias=output/'input-alias.json';os.link(repo/'Content/Rig.uasset',alias);original_alias=alias.read_bytes()
 def protected(*args):
  global checks
  r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(alias),*map(str,args)],env=env,capture_output=True,timeout=60)
  assert r.returncode==2 and alias.read_bytes()==original_alias,(args,r.returncode);checks+=1
 protected(*base)
 protected('--before',fixtures/'animation-0-1013.uasset','--new-document',*base,'--document-index','0')
 alias.unlink();os.link(repo/'Game.uproject',alias);original_alias=alias.read_bytes()
 protected(*mounted)
 alias.unlink()
 assert all(p.read_bytes()==data for p,data in originals.items()) and git('status','--porcelain=v1')==status
 assert not (root/'unused-profile').exists()
print(f'{checks} native historical animation pose checks passed')
