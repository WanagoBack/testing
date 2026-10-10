"""Download a public MEGA file for offline conversion (no account or quota bypass).
Requires cryptography. Uses verified HTTPS; checks length, not MEGA's whole-file MAC.
"""
import base64,json,pathlib,re,urllib.request

def download(link,destination):
 from cryptography.hazmat.primitives.ciphers import Cipher,algorithms,modes
 match=re.fullmatch(r'https://mega\.(?:nz|co\.nz)/(?:file/([\w-]{8})#([\w-]{43})|#!([\w-]{8})!([\w-]{43}))',link)
 if not match:raise ValueError('A complete public MEGA file link is required')
 handle=match[1] or match[3];encoded=match[2] or match[4]
 key=base64.urlsafe_b64decode(encoded+'=');aes=bytes(key[i]^key[i+16] for i in range(16))
 body=json.dumps([{'a':'g','g':1,'p':handle,'ssl':2}]).encode()
 req=urllib.request.Request('https://g.api.mega.co.nz/cs?id=1',data=body,headers={'Content-Type':'application/json'})
 with urllib.request.urlopen(req,timeout=30) as r:meta=json.load(r)[0]
 if not isinstance(meta,dict):raise RuntimeError('MEGA file unavailable or transfer limit reached (%s)'%meta)
 url=meta.get('g','');size=meta.get('s',0)
 if not isinstance(url,str) or not url.startswith('https://') or not isinstance(size,int) or size<=0:raise RuntimeError('Invalid MEGA response')
 decrypt=Cipher(algorithms.AES(aes),modes.CTR(key[16:24]+bytes(8))).decryptor()
 destination=pathlib.Path(destination);temp=destination.with_suffix('.download')
 try:
  with urllib.request.urlopen(url+'/0-'+str(size-1),timeout=60) as response,temp.open('wb') as out:
   received=0
   while True:
    chunk=response.read(1024*1024)
    if not chunk:break
    received+=len(chunk)
    if received>size:raise RuntimeError('Unexpected download length')
    out.write(decrypt.update(chunk))
   out.write(decrypt.finalize())
  if received!=size:raise RuntimeError('Incomplete download')
  temp.replace(destination)
 except BaseException:
  temp.unlink(missing_ok=True);raise
