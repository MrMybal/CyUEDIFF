"""Native source skin previews, localized typed inspection and persisted filters."""
import json,os,subprocess,sys,tempfile
from pathlib import Path
exe,fixture=map(lambda p:str(Path(p).resolve()),sys.argv[1:])
checks=0
env={k.upper():v for k,v in os.environ.items()}
with tempfile.TemporaryDirectory(prefix='.test-tmp-skin-desktop-',dir=Path.cwd()) as tmp:
    root=Path(tmp);env['LOCALAPPDATA']=str(root/'unused-profile')
    subprocess.run([fixture,'--write-fixtures',str(root)],capture_output=True,check=True,env=env,timeout=30)
    report=root/'report.json';workspace=root/'workspace.json'
    def run(*args):
        global checks
        report.unlink(missing_ok=True)
        result=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],capture_output=True,env=env,timeout=45)
        assert result.returncode==0,(result.returncode,result.stderr.decode('utf-8','replace'),args)
        checks+=1
        return json.loads(report.read_text(encoding='utf-8'))
    state={'asset':{'meshSkinInspection':True,'meshSkin':{'filter':'Default','selected':'/vertices/0/Default/root','changedOnly':True}}}
    action='view-state:'+json.dumps(state,separators=(',',':'))
    first=run('--before',root/'skin-before.uasset','--after',root/'skin-after.uasset','--view','meshes','--document-action',action,'--save-workspace',workspace)
    assert first['document']['beforeMesh']=={'vertices':3,'triangles':1,'sourceBones':2,'skinProfiles':2}
    assert first['document']['afterMesh']==first['document']['beforeMesh']
    actual=first['workspace']['documents'][0]['viewState']['asset']
    assert actual['meshSkinInspection'] and actual['meshSkin']['filter']=='Default' and actual['meshSkin']['changedOnly']
    restored=run('--workspace',workspace,'--locale','fr')
    assert restored['document']==first['document']
    assert restored['workspace']['documents'][0]['viewState']['asset']['meshSkin']==actual['meshSkin']
    virtual=run('--before',root/'skin-virtualized.uasset','--payload-root',root/'payloads','--view','meshes')
    assert virtual['document']['beforeMesh']['sourceBones']==2
print(f'{checks} source skin Desktop checks passed')
