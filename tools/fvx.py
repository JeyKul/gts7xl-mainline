# Extract PE modules from firmware volumes inside a Qualcomm XBL ELF.
import sys,struct,lzma,os,uuid,hashlib,zlib
src,out=sys.argv[1],sys.argv[2]; os.makedirs(out,exist_ok=True)
data=open(src,'rb').read()
FVS='_FVH'.encode()
found={}
def sec(buf,name=None,depth=0):
    i=0
    res=[]
    while i+4<=len(buf):
        sz=int.from_bytes(buf[i:i+3],'little'); t=buf[i+3]
        h=4
        if sz==0xffffff:
            sz=struct.unpack('<I',buf[i+4:i+8])[0]; h=8
        if sz<h or i+sz>len(buf): break
        body=buf[i+h:i+sz]
        res.append((t,body))
        i=(i+sz+3)&~3
    return res
def walk_sections(buf,guid,depth=0):
    r={}
    for t,b in sec(buf):
        if t==0x15:
            try:r['name']=b.decode('utf-16le').rstrip('\0')
            except: pass
        elif t==0x10: r['pe']=b
        elif t==0x1b or t==0x13: r['depex']=b
        elif t==0x19: r['raw']=b
        elif t==0x17:
            parse_fv(b)
        elif t==0x02:
            g=uuid.UUID(bytes_le=b[:16]); off=struct.unpack('<H',b[16:18])[0]
            pay=b[off-4:]
            try:
                d=lzma.decompress(pay,format=lzma.FORMAT_ALONE)
            except Exception:
                try: d=zlib.decompress(pay,47)
                except Exception: continue
            s=walk_sections(d,guid,depth+1); r={**r,**s}
    return r
def parse_fv(b):
    if b[40:44]!=FVS: return
    hl=struct.unpack('<H',b[48:50])[0]; ln=struct.unpack('<Q',b[32:40])[0]
    i=hl; ln=min(ln,len(b))
    while i+24<=ln:
        hdr=b[i:i+24]; g=uuid.UUID(bytes_le=hdr[:16]); t=hdr[18]; sz=int.from_bytes(hdr[20:23],'little'); h=24
        if hdr[:16]==b'\xff'*16 and t==0xff: break
        if sz==0xffffff and t!=0xff:
            sz=struct.unpack('<Q',b[i+24:i+32])[0]; h=32
        if sz<h: break
        body=b[i+h:i+sz]
        if t in (3,4,5,6,7,8,9,0xb,0xf):
            r=walk_sections(body,g)
            if r.get('pe') or r.get('raw'):
                nm=r.get('name',str(g))
                if t==2: pass
                found[str(g)]=(nm,t,r)
        elif t==2: found[str(g)]=(str(g),t,{'raw':body})
        i=(i+sz+7)&~7
i=0
while True:
    i=data.find(FVS,i)
    if i<0: break
    s=i-40
    try: parse_fv(data[s:])
    except Exception as e: print('fv err',hex(s),e)
    i+=4
for g,(n,t,r) in sorted(found.items(),key=lambda x:x[1][0]):
    nm=''.join(c for c in n if c.isalnum() or c in '._-') or g
    for k,e in (('pe','.efi'),('depex','.depex'),('raw','.raw')):
        if k in r: open(os.path.join(out,nm+e),'wb').write(r[k])
print('modules',len(found))
