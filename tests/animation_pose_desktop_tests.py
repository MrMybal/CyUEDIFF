"""Native explicit Skeleton loading, frame contexts, persisted review and protected outputs."""
import hashlib,json,os,subprocess,sys,tempfile,shutil
from pathlib import Path
exe,generator=(str(Path(x).resolve()) for x in sys.argv[1:3]);checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-animation-pose-desktop-',dir=Path.cwd()) as tmp:
 root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
 subprocess.run([generator,'--write-fixtures',str(root)],check=True,env=env)
 a=root/'animation-0-1013.uasset';b=root/'animation-1-1013.uasset';s=root/'skeleton-0.uasset'
 hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.glob('*.uasset')}
 report=root/'report.json';workspace=root/'workspace.json'
 def run(*args,code=0):
  global checks
  report.unlink(missing_ok=True)
  r=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
  assert r.returncode==code,(args,r.returncode,r.stderr.decode('utf-8','replace'));checks+=1
  return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
 def pose(data):return data['document']['animationPose']
 base=['--before',a,'--after',b,'--view','animations','--no-previews','--pose-before-skeleton',s,'--pose-after-skeleton',s]
 d=run(*base,'--pose-frame','1');assert pose(d)['complete'] and pose(d)['comparison']['changedJoints']==1 and pose(d)['before']['frame']==1
 state={'asset':{'animationPose':{'selected':'arm~/track','filter':'arm','positionTolerance':.5,'camera':{'target':[20,20,20],'distance':80,'yaw':.5,'pitch':.3}},'animationPosePreview':True}}
 d=run(*base,'--pose-before-frame','1','--pose-after-frame','2','--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace)
 saved=json.loads(workspace.read_text(encoding='utf-8'));setting=saved['documents'][0]['settings']
 assert saved['schema']=='cyuediff.workspace/6' and setting['pose_enabled'] and setting['pose_before_frame']==1 and setting['pose_after_frame']==2 and setting['pose_independent_frames']
 actual=saved['documents'][0]['viewState']['asset']['animationPose'];assert actual['selected']=='arm~/track' and actual['positionTolerance']==.5
 previous=workspace.read_bytes();reopened=run('--workspace',workspace);assert pose(reopened)['complete'] and pose(reopened)['after']['frame']==2 and workspace.read_bytes()==previous
 assert reopened['workspace']['documents'][0]['viewState']['asset']['animationPose']==actual
 reloaded=run('--workspace',workspace,'--document-action','reload');assert reloaded['workspace']['documents'][0]['revision']==2 and reloaded['workspace']['documents'][0]['viewState']['asset']['animationPose']==actual
 raw=run('--workspace',workspace,'--document-action','view-state:'+json.dumps({'asset':{'animationPosePreview':False}}));assert not raw['workspace']['documents'][0]['viewState']['asset']['animationPosePreview']
 single=run('--before',a,'--view','animations','--pose-before-skeleton',s,'--pose-frame','1');assert pose(single)['status']=='ready' and pose(single)['after'] is None
 missing=run('--before',a,'--after',b,'--pose-before-skeleton',s,'--pose-after-skeleton',root/'missing.uasset');assert pose(missing)['status']=='incomplete' and pose(missing)['comparison']['joints']['arm~/track']['kind']=='unavailable'
 invalid=run(*base,'--pose-before-object','missing');assert pose(invalid)['status']=='incomplete' and pose(invalid)['diagnostics']
 outside=run(*base,'--pose-before-frame','0','--pose-after-frame','3');assert pose(outside)['status']=='incomplete' and pose(outside)['after']['frame']==3
 multiple=run(*base,'--pose-frame','1','--new-document','--before',a,'--pose-before-skeleton',s,'--pose-frame','2')
 docs=multiple['workspace']['documents'];assert len(docs)==2 and docs[0]['document']['animationPose']['before']['frame']==1 and docs[1]['document']['animationPose']['before']['frame']==2
 duplicate=run('--workspace',workspace,'--document-action','duplicate');assert len(duplicate['workspace']['documents'])==2 and pose(duplicate)['after']['frame']==2
 assert duplicate['workspace']['documents'][1]['viewState']['asset']['animationPose']==actual
 closed=run('--workspace',workspace,'--document-action','close','--document-action','reopen');assert pose(closed)['after']['frame']==2 and closed['workspace']['documents'][-1]['viewState']['asset']['animationPose']==actual
 for side,source in (('before',a),('after',b)):
  content=root/side/'Content';content.mkdir(parents=True);(root/side/'Test.uproject').write_text('{"FileVersion":3}',encoding='utf-8')
  shutil.copyfile(source,content/'Clip.uasset');shutil.copyfile(s,content/'Rig.uasset')
 mounted=run('--before','/Game/Clip','--after','/Game/Clip','--before-project',root/'before/Test.uproject','--after-project',root/'after/Test.uproject','--pose-before-skeleton','/Game/Rig','--pose-after-skeleton','/Game/Rig','--pose-frame','1')
 assert pose(mounted)['complete'] and pose(mounted)['comparison']['changedJoints']==1 and pose(mounted)['beforeSkeleton']!=pose(mounted)['afterSkeleton']
 draft=json.loads(workspace.read_text(encoding='utf-8'));draft_settings=draft['documents'][0]['settings']
 draft_settings.update({'before':'/Game/Clip','after':'/Game/Clip','project':str(root/'before/Test.uproject'),'after_project':str(root/'after/Test.uproject'),'pose_enabled':False,'pose_before_skeleton':'/Game/Missing','pose_after_skeleton':'/Game/MissingAfter'})
 draft_file=root/'disabled-binding.json';draft_file.write_text(json.dumps(draft),encoding='utf-8')
 disabled=run('--workspace',draft_file,'--save-workspace',root/'disabled-saved.json');assert not pose(disabled)['requested']
 draft_settings.update({'pose_enabled':True,'after':'','pose_before_skeleton':'/Game/Rig'})
 draft_file.write_text(json.dumps(draft),encoding='utf-8')
 single_draft=run('--workspace',draft_file);assert pose(single_draft)['complete'] and pose(single_draft)['after'] is None
 for args in [('--before',a,'--pose-frame','0'),('--before',a,'--after',b,'--pose-before-skeleton',s),(*base,'--pose-frame','-1'),(*base,'--pose-frame','1.5'),(*base,'--pose-frame','2147483648'),(*base,'--pose-before-frame','0'),(*base,'--pose-after-frame','0'),(*base,'--pose-frame','0','--pose-before-frame','0','--pose-after-frame','0'),('--container',s,'--pose-before-skeleton',s),('--repo',root,'--before','Clip.uasset','--pose-before-skeleton',s)]:run(*args,code=2)
 # A Skeleton can have an arbitrary filename; protect it before the JSON output is opened.
 rig_json=root/'rig-input.json';shutil.copyfile(s,rig_json);rig_hash=hashlib.sha256(rig_json.read_bytes()).hexdigest()
 bad=subprocess.run([exe,'--smoke','--no-preferences','--before',str(a),'--pose-before-skeleton',str(rig_json),'--report',str(rig_json)],env=env,capture_output=True,timeout=45);assert bad.returncode==2 and hashlib.sha256(rig_json.read_bytes()).hexdigest()==rig_hash;checks+=1
 # The inactive tab still protects its selected Skeleton against another document's output.
 bad=subprocess.run([exe,'--smoke','--no-preferences','--before',str(a),'--report',str(rig_json),'--new-document','--before',str(a),'--pose-before-skeleton',str(rig_json),'--document-index','0'],env=env,capture_output=True,timeout=45);assert bad.returncode==2 and hashlib.sha256(rig_json.read_bytes()).hexdigest()==rig_hash;checks+=1
 assert all(hashlib.sha256(p.read_bytes()).hexdigest()==h for p,h in hashes.items());assert not (root/'unused-profile').exists()
print(f'{checks} native animation pose/workspace checks passed')
