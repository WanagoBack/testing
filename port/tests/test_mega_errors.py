"""Host HTTPS fixtures: quota/deleted/malformed responses, no quota retries, >2GiB seek.
Requires host GCC, libcurl, libavutil.so.58, openssl, and cryptography.
"""
import pathlib,tempfile,subprocess,ssl,http.server,threading,json,base64,struct
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
root=pathlib.Path(__file__).resolve().parents[1]
key=bytes(range(32));keystr=base64.urlsafe_b64encode(key).decode().rstrip('=');aes=bytes(key[i]^key[i+16] for i in range(16));size=3*1024**3+123
hits={}
class Server(http.server.BaseHTTPRequestHandler):
 def log_message(self,*a):pass
 def reply(self,code,body):
  self.send_response(code);self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
 def do_POST(self):
  request=json.loads(self.rfile.read(int(self.headers['Content-Length'])))[0];h=request['p']
  value=-17 if h=='quota000' else -9 if h=='gone0000' else {'g':'https://localhost:%d/%s'%(self.server.server_port,h),'s':size}
  self.reply(200,b'invalid' if h=='badjson0' else json.dumps([value]).encode())
 def do_GET(self):
  _,h,span=self.path.split('/');hits[h]=hits.get(h,0)+1
  if h=='http5090':self.reply(509,b'quota');return
  if h=='short000':self.reply(200,b'x');return
  a,b=map(int,span.split('-'));plain=bytes(range(251));n=b-a+1
  data=(plain*((n+250)//251+2))[a%251:a%251+n]
  data=Cipher(algorithms.AES(aes),modes.CTR(key[16:24]+(a//16).to_bytes(8,'big'))).encryptor().update(data)
  self.reply(200,data)
with tempfile.TemporaryDirectory() as d:
 cert=d+'/cert.pem';private=d+'/key.pem'
 subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-keyout',private,'-out',cert,'-days','1','-subj','/CN=localhost','-addext','subjectAltName=DNS:localhost'],check=True,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
 server=http.server.ThreadingHTTPServer(('127.0.0.1',0),Server);ctx=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);ctx.load_cert_chain(cert,private);server.socket=ctx.wrap_socket(server.socket,server_side=True);threading.Thread(target=server.serve_forever,daemon=True).start()
 common=['gcc','-std=c99','-I'+str(root/'include'),'-I'+str(root/'library/include'),'-DWMEGA_CA_FILE="'+cert+'"','-DWMEGA_API="https://localhost:%d/cs"'%server.server_port,str(root/'source/wgb_mega.c')]
 for name in ['mega_errors','mega_probe']:
  subprocess.run(common+[str(root/'tests'/(name+'.c')),'-lcurl','-l:libavutil.so.58','-o',d+'/'+name],check=True)
 for handle,error in [('quota000','overdrachtslimiet'),('gone0000','verwijderd'),('badjson0','ongeldig'),('http5090','overdrachtslimiet'),('short000','Verbinding')]:
  subprocess.run([d+'/mega_errors','https://mega.nz/file/'+handle+'#'+keystr,error],check=True)
 assert hits['http5090']==1 and hits['short000']==3
 out=d+'/ranges.bin';subprocess.run([d+'/mega_probe','https://mega.nz/file/large000#'+keystr,out],check=True)
 data=pathlib.Path(out).read_bytes();i=0
 while i<len(data):
  pos,n=struct.unpack_from('<qi',data,i);i+=12;assert data[i:i+n]==bytes((pos+j)%251 for j in range(n));i+=n
 server.shutdown();print('PASS: verified HTTPS, quota, deleted link, malformed metadata, short responses, bounded retry, >2 GiB seek/decryption.')
