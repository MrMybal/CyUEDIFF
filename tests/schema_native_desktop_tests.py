"""Native structs through hidden desktop inspection, comparison and workspace restarts."""
import hashlib,json,os,subprocess,sys,tempfile
from pathlib import Path
exe,fixture=[str(Path(p).resolve()) for p in sys.argv[1:]]
env={k.upper():v for k,v in os.environ.items()};checks=0
with tempfile.TemporaryDirectory(prefix='.test-tmp-native-desktop-',dir=Path.cwd()) as folder:
    root=Path(folder);env['LOCALAPPDATA']=str(root/'unused-profile')
    subprocess.run([fixture,'--write-fixtures',str(root)],env=env,check=True,capture_output=True,timeout=20)
    hashes={p:hashlib.sha256(p.read_bytes()).hexdigest() for p in root.iterdir()}
    entry='000000000000000000000001';field='/Script/Test.Collections.Lookup';report=root/'report.json'
    base=['--container',root/'native.utoc','--container-entry',entry,'--cooked-profile','ue5.3','--script-objects',root/'global.utoc','--schema',root/'native.json','--cooked-view','properties']
    def run(*args,code=0):
        global checks
        report.unlink(missing_ok=True)
        result=subprocess.run([exe,'--smoke','--no-preferences','--report',str(report),*map(str,args)],env=env,capture_output=True,timeout=45)
        assert result.returncode==code,(result.returncode,result.stderr.decode('utf-8','replace'),args)
        checks+=1
        return json.loads(report.read_text(encoding='utf-8')) if report.exists() else None
    first=run(*base)
    data=first['document']['container'];prop=data['zen']['exports'][0]['propertyRead']['properties'][field]
    assert prop['nativeLayout']=='transform-f64' and prop['value']['translation'][0]==1000000000000.125;checks+=1
    assert first['containerView']['objectProperties']['available'] and first['containerView']['objectProperties']['visibleProperties']>10
    assert data['schemaContext']['nativeStructs']=={'/Script/Test.Record':'transform-f64'} and data['schemaContext']['format']=='cyuediff.zen-schema/2';checks+=1
    paired=run(*base,'--after-container',root/'changed.utoc','--after-cooked-profile','ue5.3','--after-script-objects',root/'global.utoc','--after-schema',root/'native.json')
    assert paired['containerView']['objectProperties']['comparing']
    selected=paired['document']['container']['selectedComparison']['zen']
    assert any(c['path'].endswith('/value/translation/1') for c in selected['changes']);checks+=1
    unbound=run(*base,'--schema',root/'unbound.json')
    assert not unbound['containerView']['objectProperties']['available'] and unbound['containerView']['objectProperties']['beforeStatus']=='opaque';checks+=1
    invalid=run(*base,'--container',root/'truncated.utoc')
    assert not invalid['containerView']['objectProperties']['available'];checks+=1
    saved=root/'workspace.json';run(*base,'--save-workspace',saved)
    resumed=run('--workspace',saved,'--locale','fr')
    assert resumed['document']==first['document'] and resumed['containerView']['objectProperties']['available'];checks+=1
    modern=run(*base,'--container',root/'native55.utoc','--cooked-profile','ue5.5','--schema',root/'native55.json')
    assert modern['document']['container']['zen']['propertyExportsDecoded']==2;checks+=1
    provenance=run(*base,'--container-tab','schema')
    assert provenance['containerView']['tab']==4;checks+=1
    for layout,leaf in (('date-time-i64','ticks'),('timespan-i64','ticks'),('box3-f64','isValid'),('sphere-f64','radius'),('box-sphere-f64','sphereRadius')):
        selected='/~1Script~1Test.Collections.Lookup/'+leaf
        state={'container':{'values':{'filter':leaf,'selected':selected}}}
        workspace=root/(layout+'-workspace.json')
        paired=run(*base,'--container',root/(layout+'.utoc'),'--cooked-profile','ue5.5','--schema',root/(layout+'.json'),
                   '--after-container',root/('changed-'+layout+'.utoc'),'--after-cooked-profile','ue5.5',
                   '--after-script-objects',root/'global.utoc','--after-schema',root/(layout+'.json'),
                   '--document-action','view-state:'+json.dumps(state),'--save-workspace',workspace)
        assert paired['containerView']['objectProperties']['available'] and paired['containerView']['objectProperties']['comparing'];checks+=1
        resumed=run('--workspace',workspace,'--locale','fr')
        assert resumed['document']==paired['document'] and resumed['workspace']['documents'][0]['viewState']['container']['values']['selected']==selected;checks+=1
    for protected in ('native.json','native.usmap','native.utoc','native.ucas'):
        run(*base,'--save-workspace',root/protected,code=2)
    assert all(hashlib.sha256(p.read_bytes()).hexdigest()==digest for p,digest in hashes.items()) and not (root/'unused-profile').exists();checks+=1
print(f'{checks} native schema desktop checks passed')
