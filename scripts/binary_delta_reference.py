"""Independent stdlib-only interpretation of JSON/compact byte delta contracts for validation."""
import hashlib,json,struct
def decode(raw):
    if not raw.startswith(b'CYUEDLT\0'):
        return json.loads(raw)
    magic,version,flags,block,reserved,base,target,count,a,b=struct.unpack_from('<8sIIIIQQQ32s32s',raw)
    assert version==1 and flags==reserved==0 and 16<=block<=1024*1024
    at=112;ops=[]
    assert count<=(len(raw)-at)//9
    for _ in range(count):
        tag=raw[at];at+=1
        if tag==0:
            offset,n=struct.unpack_from('<QQ',raw,at);at+=16
            assert n and offset<=base and n<=base-offset
            ops.append({'kind':'copy','offset':offset,'bytes':n})
        else:
            assert tag==1
            n,=struct.unpack_from('<Q',raw,at);at+=8
            assert n and n<=len(raw)-at
            ops.append({'kind':'literal','hex':raw[at:at+n].hex()});at+=n
    assert at==len(raw)
    return {'schema':'cyuediff.binary-delta/1','blockSize':block,'base':{'bytes':base,'sha256':a.hex()},
            'target':{'bytes':target,'sha256':b.hex()},'operations':ops}
def reconstruct(base,manifest):
    assert manifest['schema']=='cyuediff.binary-delta/1'
    assert manifest['base']=={'bytes':len(base),'sha256':hashlib.sha256(base).hexdigest()}
    output=bytearray();copied=literal=0
    for op in manifest['operations']:
        if op['kind']=='copy':
            at,n=op['offset'],op['bytes']
            assert at>=0 and n>0 and at<=len(base) and n<=len(base)-at
            output+=base[at:at+n];copied+=n
        else:
            assert op['kind']=='literal';data=bytes.fromhex(op['hex']);assert data
            output+=data;literal+=len(data)
    assert manifest['target']=={'bytes':len(output),'sha256':hashlib.sha256(output).hexdigest()}
    return output,copied,literal
