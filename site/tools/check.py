"""Validate the generated static bundle, translation parity, links, image provenance and hashes."""
import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import re
from urllib.parse import urlsplit, unquote
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parents[2]
p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--output', type=Path, default=ROOT/'build-site/pages')
p.add_argument('--url', help='Optional running local preview for HTTP availability checks')
a = p.parse_args()
out = a.output.resolve(strict=True)
source = ROOT/'site'
class Page(HTMLParser):
    def __init__(self): super().__init__();self.ids=set();self.refs=[];self.headings=0;self.zoom=0
    def handle_starttag(self,tag,attrs):
        d=dict(attrs)
        if 'id' in d:
            assert d['id'] not in self.ids,('Duplicate ID',d['id'])
            self.ids.add(d['id'])
        if tag=='h1':self.headings+=1
        if 'data-zoom' in d:
            assert d.get('aria-label');self.zoom+=1
        if tag=='img':assert 'alt' in d
        for key in ('src','href'):
            if key in d:self.refs.append((tag,d[key]))
pages={}
for name in ('index.html','guide.html'):
    text=(out/name).read_text(encoding='utf-8')
    assert '{{' not in text
    page=Page();page.feed(text);assert page.headings==1
    pages[name]=page
checks=0
for name,page in pages.items():
    for tag,ref in page.refs:
        url=urlsplit(ref)
        if url.scheme or url.netloc:continue
        target=(out/(unquote(url.path) or name)).resolve()
        assert target.is_relative_to(out) and target.is_file(),(name,ref)
        if url.fragment and target.suffix=='.html':assert unquote(url.fragment) in pages[target.name].ids,(name,ref)
        checks+=1
    assert page.zoom==0 or page.zoom>=16
content=json.loads((source/'content.json').read_text(encoding='utf-8'))
assert content['translations']['en'].keys()==content['translations']['fr'].keys()
for lang,entries in content['translations'].items():
    assert all(isinstance(value,str) and value.strip() for value in entries.values()),lang
    assert 'C:/Users/' not in json.dumps(entries) and 'CyberAlien' not in json.dumps(entries)
manifest=json.loads((out/'asset-manifest.json').read_text(encoding='utf-8'))
for value in manifest['assets'].values():
    data=(out/value).read_bytes()
    assert hashlib.sha256(data).hexdigest()[:12] in value,value
captures=json.loads((source/'assets/capture-manifest.json').read_text(encoding='utf-8'))
assert captures['privateProjectData'] is False
for item in captures['records']:
    data=(out/'assets/screenshots'/item['file']).read_bytes()
    assert hashlib.sha256(data).hexdigest()==item['sha256']
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    checks+=1
for name in content['shots']:
    for lang in ('en','fr'):assert (out/f'assets/screenshots/{name}-{lang}.png').is_file()
if manifest['download']:
    info=manifest['download'];data=(out/info['href']).read_bytes()
    assert len(data)==info['bytes'] and hashlib.sha256(data).hexdigest()==info['sha256']
else:
    assert not (out/'downloads').exists(),'Unrequested local archives must not survive a public build'
assert (out/'.nojekyll').is_file()
# Runtime resources are local, including both classic scripts: no CDN, fetch or module/file CORS dependency.
for page in pages.values():
    assert all(not urlsplit(ref).scheme for tag,ref in page.refs if tag in ('img','script','link'))
if a.url:
    assert urlsplit(a.url).hostname in ('127.0.0.1','localhost','::1')
    paths=['index.html','guide.html',*manifest['assets'].values()]
    if manifest['download']:paths.append(manifest['download']['href'])
    for path in paths:
        with urlopen(a.url.rstrip('/')+'/'+path,timeout=20) as response:
            assert response.status==200
            if path.endswith('.zip'):assert hashlib.sha256(response.read()).hexdigest()==manifest['download']['sha256']
            checks+=1
print(f'{checks} static resource/link/provenance checks passed; {len(content["translations"]["en"])} complete keys per language, {len(captures["records"])} authentic captures.')
