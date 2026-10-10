/* Public MEGA file streaming, GPL-3.0-or-later. No account or proxy required.
 * Independent implementation of public-file g/ssl=2 requests and AES-CTR ranges.
 * Partial reads cannot verify MEGA's whole-file MAC; HTTPS is always verified. */
#include "wgb_mega.h"
#include <curl/curl.h>
#include "libavutil/aes_ctr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>
#define JSMN_STATIC
#include "jsmn.h"
#define BLOCK (512u*1024u)
#define META_CAP 16384
#ifndef WMEGA_CA_FILE
#define WMEGA_CA_FILE "romfs:/gfx/cert/cacert.pem"
#endif
#ifndef WMEGA_API
#define WMEGA_API "https://g.api.mega.co.nz/cs?id=1"
#endif
struct Wmega_stream {
 CURL* curl;struct AVAESCTR* aes;uint8_t nonce[8];char handle[9],url[2048];
 int64_t size,pos,start[2];size_t length[2];uint8_t* block[2];unsigned next,generation;
};
static unsigned cancelled,busy,error_code,generation;
enum { E_NONE,E_LINK,E_NET,E_QUOTA,E_GONE,E_REPLY,E_MEMORY,E_KEY,E_CANCEL };
static void error(unsigned n){__atomic_store_n(&error_code,n,__ATOMIC_RELAXED);}
static bool stopped(const Wmega_stream* s){return __atomic_load_n(&cancelled,__ATOMIC_RELAXED)!=0 || (s && s->generation!=__atomic_load_n(&generation,__ATOMIC_RELAXED));}
void Wmega_begin(void){__atomic_add_fetch(&generation,1,__ATOMIC_RELAXED);__atomic_store_n(&cancelled,0,__ATOMIC_RELAXED);error(E_NONE);}
void Wmega_cancel(void){__atomic_store_n(&cancelled,1,__ATOMIC_RELAXED);}
bool Wmega_buffering(void){return __atomic_load_n(&busy,__ATOMIC_RELAXED)!=0;}
const char* Wmega_error(void){
 switch(__atomic_load_n(&error_code,__ATOMIC_RELAXED)){
 case E_LINK:return "Ongeldige MEGA-bestandslink of ontbrekende sleutel.";
 case E_NET:return "Verbinding verbroken. Ga terug en probeer opnieuw.";
 case E_QUOTA:return "De overdrachtslimiet is bereikt. Probeer later opnieuw.";
 case E_GONE:return "Deze video is verwijderd of niet beschikbaar.";
 case E_REPLY:return "De videoserver gaf een ongeldig antwoord.";
 case E_MEMORY:return "Niet genoeg geheugen om de video te openen.";
 case E_KEY:return "De videosleutel is ongeldig.";
 case E_CANCEL:return "";
 default:return "";
 }
}
static int b64(char c){if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;if(c>='0'&&c<='9')return c-'0'+52;if(c=='-')return 62;if(c=='_')return 63;return -1;}
static bool parse_link(const char* link,char handle[9],uint8_t key[32]){
 const char* p=NULL;char sep='#';
 if(!link)return false;
 if(!strncmp(link,WMEGA_PREFIX,8))p=link+8;
 else if(!strncmp(link,"https://mega.nz/file/",21))p=link+21;
 else if(!strncmp(link,"https://mega.co.nz/file/",24))p=link+24;
 else if(!strncmp(link,"https://mega.nz/#!",17)){p=link+17;sep='!';}
 else if(!strncmp(link,"https://mega.co.nz/#!",20)){p=link+20;sep='!';}
 if(!p||strlen(p)!=52||p[8]!=sep)return false;
 for(int i=0;i<8;i++)if(b64(p[i])<0)return false;
 unsigned acc=0,bits=0,n=0;
 for(int i=9;i<52;i++){int v=b64(p[i]);if(v<0)return false;acc=(acc<<6)|(unsigned)v;bits+=6;if(bits>=8){bits-=8;if(n>=32)return false;key[n++]=(uint8_t)(acc>>bits);}}
 if(n!=32||(acc&3))return false;
 memcpy(handle,p,8);handle[8]=0;return true;
}
bool Wmega_path(const char* link,char* path,size_t capacity){
 char handle[9];uint8_t key[32];
 if(!path||capacity<61||!parse_link(link,handle,key)){error(E_LINK);return false;}
 const char* hash=strchr(link,'#');const char* k=!strncmp(link,WMEGA_PREFIX,8)?link+17:(hash&&hash[1]=='!'?hash+11:hash+1);
 snprintf(path,capacity,WMEGA_PREFIX "%s#%.43s",handle,k);memset(key,0,sizeof(key));return true;
}
static int progress(void* opaque,curl_off_t a,curl_off_t b,curl_off_t c,curl_off_t d){(void)opaque;(void)a;(void)b;(void)c;(void)d;return stopped(opaque)?1:0;}
static CURL* connection(Wmega_stream* s){
 CURL* c=curl_easy_init();if(!c)return NULL;
 curl_easy_setopt(c,CURLOPT_CAINFO,WMEGA_CA_FILE);
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,3L);
 curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,10L);
 curl_easy_setopt(c,CURLOPT_TIMEOUT,40L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,256L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,15L);
 curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);
 curl_easy_setopt(c,CURLOPT_XFERINFODATA,s);
 curl_easy_setopt(c,CURLOPT_USERAGENT,"WanagoBack-3DS/0.3");
 return c;
}
typedef struct {uint8_t* data;size_t used,limit;Wmega_stream* owner;} Buffer;
static size_t write_data(char* ptr,size_t a,size_t b,void* opaque){Buffer* out=opaque;if(a&&b>SIZE_MAX/a)return 0;size_t n=a*b;if(stopped(out->owner)||n>out->limit-out->used)return 0;memcpy(out->data+out->used,ptr,n);out->used+=n;return n;}
static bool token_eq(const char* json,const jsmntok_t* t,const char* key){return t->type==JSMN_STRING&&(size_t)(t->end-t->start)==strlen(key)&&!memcmp(json+t->start,key,strlen(key));}
static bool metadata(Wmega_stream* s){
 char* json=calloc(1,META_CAP+1);if(!json){error(E_MEMORY);return false;}
 Buffer out={(uint8_t*)json,0,META_CAP,s};char req[96];snprintf(req,sizeof(req),"[{\"a\":\"g\",\"g\":1,\"p\":\"%s\",\"ssl\":2}]",s->handle);
 curl_easy_setopt(s->curl,CURLOPT_URL,WMEGA_API);curl_easy_setopt(s->curl,CURLOPT_POSTFIELDS,req);
 curl_easy_setopt(s->curl,CURLOPT_WRITEFUNCTION,write_data);curl_easy_setopt(s->curl,CURLOPT_WRITEDATA,&out);
 CURLcode rc=curl_easy_perform(s->curl);long http=0;curl_easy_getinfo(s->curl,CURLINFO_RESPONSE_CODE,&http);
 curl_easy_setopt(s->curl,CURLOPT_POSTFIELDS,NULL);curl_easy_setopt(s->curl,CURLOPT_HTTPGET,1L);
 bool ok=false;
 if(rc!=CURLE_OK||http!=200){error(stopped(s)?E_CANCEL:http==509?E_QUOTA:E_NET);goto end;}
 jsmn_parser parser;jsmntok_t tokens[128];jsmn_init(&parser);int n=jsmn_parse(&parser,json,out.used,tokens,128);
 if(n<2||tokens[0].type!=JSMN_ARRAY||tokens[0].size!=1){error(E_REPLY);goto end;}
 if(tokens[1].type==JSMN_PRIMITIVE){int code=atoi(json+tokens[1].start);error(code==-17?E_QUOTA:code==-9?E_GONE:E_REPLY);goto end;}
 int64_t length=0;char url[2048]={0};
 for(int i=2;i+1<n;i++){
  if(token_eq(json,&tokens[i],"s")&&tokens[i+1].type==JSMN_PRIMITIVE){char* endptr;errno=0;length=strtoll(json+tokens[i+1].start,&endptr,10);if(errno||endptr!=json+tokens[i+1].end)length=0;}
  if(token_eq(json,&tokens[i],"g")&&tokens[i+1].type==JSMN_STRING){size_t j=0;for(int p=tokens[i+1].start;p<tokens[i+1].end&&j<sizeof(url)-1;p++){char c=json[p];if(c=='\\'&&p+1<tokens[i+1].end&&json[p+1]=='/')c=json[++p];else if(c=='\\'){j=0;break;}url[j++]=c;}url[j]=0;}
 }
 if(length<=0||length>INT64_C(1099511627776)||strncmp(url,"https://",8)||strlen(url)>1900){error(E_REPLY);goto end;}
 if(s->size&&s->size!=length){error(E_REPLY);goto end;}
 s->size=length;snprintf(s->url,sizeof(s->url),"%s",url);ok=true;
end:free(json);return ok;
}
Wmega_stream* Wmega_open(const char* path){
 if(stopped(NULL)){error(E_CANCEL);return NULL;}
 error(E_NONE);__atomic_store_n(&busy,1,__ATOMIC_RELAXED);
 Wmega_stream* s=calloc(1,sizeof(*s));uint8_t key[32]={0};
 if(!s){error(E_MEMORY);goto fail;}
 s->generation=__atomic_load_n(&generation,__ATOMIC_RELAXED);s->aes=av_aes_ctr_alloc();if(!s->aes){error(E_MEMORY);goto fail;}s->start[0]=s->start[1]=-1;
 if(!parse_link(path,s->handle,key)){error(E_LINK);goto fail;}
 memcpy(s->nonce,key+16,8);for(int i=0;i<16;i++)key[i]^=key[i+16];
 if(av_aes_ctr_init(s->aes,key)){error(E_KEY);goto fail;}
 s->curl=connection(s);s->block[0]=malloc(BLOCK);s->block[1]=malloc(BLOCK);
 if(!s->curl||!s->block[0]||!s->block[1]){error(E_MEMORY);goto fail;}
 if(!metadata(s))goto fail;
 memset(key,0,sizeof(key));__atomic_store_n(&busy,0,__ATOMIC_RELAXED);return s;
fail:memset(key,0,sizeof(key));Wmega_close(s);__atomic_store_n(&busy,0,__ATOMIC_RELAXED);return NULL;
}
static bool fetch_block(Wmega_stream* s,unsigned slot,int64_t start){
 size_t size=(size_t)((s->size-start)<BLOCK?s->size-start:BLOCK);s->start[slot]=-1;s->length[slot]=0;
 __atomic_store_n(&busy,1,__ATOMIC_RELAXED);
 for(int attempt=0;attempt<3&&!stopped(s);attempt++){
  char url[2128];snprintf(url,sizeof(url),"%s/%lld-%lld",s->url,(long long)start,(long long)(start+(int64_t)size-1));
  Buffer out={s->block[slot],0,size,s};curl_easy_setopt(s->curl,CURLOPT_URL,url);curl_easy_setopt(s->curl,CURLOPT_WRITEFUNCTION,write_data);curl_easy_setopt(s->curl,CURLOPT_WRITEDATA,&out);
  CURLcode rc=curl_easy_perform(s->curl);long http=0;curl_easy_getinfo(s->curl,CURLINFO_RESPONSE_CODE,&http);
  if(http==509){error(E_QUOTA);break;}
  if((http==403||http==410)&&attempt==0){if(metadata(s))continue;break;}
  if(rc==CURLE_OK&&http==200&&out.used==size){
   uint8_t ctr[16]={0};memcpy(ctr,s->nonce,8);uint64_t block=(uint64_t)start/16;
   for(int i=15;i>=8;i--){ctr[i]=(uint8_t)block;block>>=8;}
   av_aes_ctr_set_full_iv(s->aes,ctr);
   av_aes_ctr_crypt(s->aes,s->block[slot],s->block[slot],(int)size);
   s->start[slot]=start;s->length[slot]=size;error(E_NONE);__atomic_store_n(&busy,0,__ATOMIC_RELAXED);return true;
  }
  error(E_NET);
  if(http==404){error(E_GONE);break;}
 }
 if(stopped(s))error(E_CANCEL);__atomic_store_n(&busy,0,__ATOMIC_RELAXED);return false;
}
int Wmega_read(Wmega_stream* s,uint8_t* data,int count){
 if(!s||!data||count<=0)return -EINVAL;if(stopped(s))return -ECANCELED;if(s->pos>=s->size)return 0;
 int slot=-1;for(int i=0;i<2;i++)if(s->pos>=s->start[i]&&s->pos<s->start[i]+(int64_t)s->length[i])slot=i;
 if(slot<0){slot=(int)(s->next++%2);int64_t start=s->pos/(int64_t)BLOCK*BLOCK;if(!fetch_block(s,(unsigned)slot,start))return stopped(s)?-ECANCELED:-EIO;}
 size_t offset=(size_t)(s->pos-s->start[slot]),n=s->length[slot]-offset;if(n>(size_t)count)n=(size_t)count;
 memcpy(data,s->block[slot]+offset,n);s->pos+=(int64_t)n;return (int)n;
}
int64_t Wmega_seek(Wmega_stream* s,int64_t offset,int whence){
 if(!s)return -EINVAL;whence&=~0x20000; /* AVSEEK_FORCE */
 if(whence==0x10000)return s->size; /* AVSEEK_SIZE */
 int64_t base=whence==SEEK_SET?0:whence==SEEK_CUR?s->pos:whence==SEEK_END?s->size:-1;
 if(base<0||(offset>0&&base>INT64_MAX-offset)||(offset<0&&offset< -base))return -EINVAL;
 int64_t target=base+offset;if(target<0||target>s->size)return -EINVAL;s->pos=target;return target;
}
void Wmega_close(Wmega_stream* s){if(!s)return;if(s->curl)curl_easy_cleanup(s->curl);if(s->aes)av_aes_ctr_free(s->aes);for(int i=0;i<2;i++){if(s->block[i]){memset(s->block[i],0,BLOCK);free(s->block[i]);}}memset(s,0,sizeof(*s));free(s);}
