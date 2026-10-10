/* One-pass catalogue reader; no platform-specific JSON path lookup. */
#include "wgb_catalog.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#define JSMN_STATIC
#include "jsmn.h"
static int skip(const jsmntok_t* t,int n,int i){int end=t[i].end;do{i++;}while(i<n&&t[i].start<end);return i;}
static int get(const char*s,const jsmntok_t*t,int n,int object,const char*key){
 for(int i=object+1;i+1<n&&t[i].start<t[object].end;){int v=i+1;if(t[i].type==JSMN_STRING&&(size_t)(t[i].end-t[i].start)==strlen(key)&&!memcmp(s+t[i].start,key,strlen(key)))return v;i=skip(t,n,v);}return -1;
}
static int hex4(const char*s){int v=0;for(int i=0;i<4;i++){char c=s[i];int d=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;if(d<0)return -1;v=v*16+d;}return v;}
static bool string(const char*s,const jsmntok_t*t,int i,char*out,size_t cap){
 if(i<0||t[i].type!=JSMN_STRING||!cap)return false;size_t used=0;
 for(int p=t[i].start;p<t[i].end;){unsigned char bytes[4];int len=1;unsigned char c=(unsigned char)s[p++];bytes[0]=c;
  if(c=='\\'){
   if(p>=t[i].end)return false;c=(unsigned char)s[p++];bytes[0]=c;
   if(c=='u'){
    if(p+4>t[i].end)return false;int v=hex4(s+p);p+=4;if(v<0)return false;
    if(v>=0xd800&&v<=0xdbff){if(p+6>t[i].end||s[p]!='\\'||s[p+1]!='u')return false;int low=hex4(s+p+2);if(low<0xdc00||low>0xdfff)return false;p+=6;v=0x10000+((v-0xd800)<<10)+(low-0xdc00);}
    else if(v>=0xdc00&&v<=0xdfff)return false;
    if(!v)return false;
    if(v<128){bytes[0]=(unsigned char)v;}else if(v<2048){len=2;bytes[0]=(unsigned char)(192|(v>>6));bytes[1]=(unsigned char)(128|(v&63));}
    else if(v<65536){len=3;bytes[0]=(unsigned char)(224|(v>>12));bytes[1]=(unsigned char)(128|((v>>6)&63));bytes[2]=(unsigned char)(128|(v&63));}
    else{len=4;bytes[0]=(unsigned char)(240|(v>>18));bytes[1]=(unsigned char)(128|((v>>12)&63));bytes[2]=(unsigned char)(128|((v>>6)&63));bytes[3]=(unsigned char)(128|(v&63));}
   }else if(c=='n')bytes[0]='\n';else if(c=='r')bytes[0]='\r';else if(c=='t')bytes[0]='\t';else if(c=='b')bytes[0]='\b';else if(c=='f')bytes[0]='\f';else if(c!='"'&&c!='\\'&&c!='/')return false;
  }else if(c>=128){len=c<224?2:c<240?3:4;if(p+len-1>t[i].end)return false;for(int j=1;j<len;j++){bytes[j]=(unsigned char)s[p++];if((bytes[j]&192)!=128)return false;}}
  if(used+(size_t)len>=cap)break;memcpy(out+used,bytes,(size_t)len);used+=(size_t)len;
 }
 out[used]=0;return true;
}
int Wcatalog_parse(const char*s,Entry*out,int capacity){
 if(!s||!out||capacity<=0)return -1;if(!strncmp(s,"\xef\xbb\xbf",3))s+=3;
 size_t len=strlen(s);if(len>1024*1024)return -1;int size=1024,n;jsmntok_t*t=NULL;
 for(;;){jsmntok_t* more=realloc(t,(size_t)size*sizeof(*t));if(!more){free(t);return -1;}t=more;jsmn_parser p;jsmn_init(&p);n=jsmn_parse(&p,s,len,t,(unsigned)size);if(n!=JSMN_ERROR_NOMEM||size>=65536)break;size*=2;}
 if(n<1||t[0].type!=JSMN_OBJECT){free(t);return -1;}int a=get(s,t,n,0,"videos");if(a<0||t[a].type!=JSMN_ARRAY){free(t);return -1;}
 int count=0;
 for(int i=a+1;i<n&&t[i].start<t[a].end&&count<capacity;i=skip(t,n,i)){
  if(t[i].type!=JSMN_OBJECT)continue;Entry e={0};
#define FIELD(name,target) string(s,t,get(s,t,n,i,name),e.target,sizeof(e.target))
  if(!FIELD("title",title)||!e.title[0])continue;
  FIELD("folder",folder);FIELD("description",description);FIELD("category",category);FIELD("url3ds",url);FIELD("megaUrl3ds",stream);
  if(!FIELD("megaUrl",source)||!e.source[0])if(!FIELD("videoUrl",source)||!e.source[0])snprintf(e.source,sizeof(e.source),"%s",e.url);
#undef FIELD
  /* Optional pre-encoded MEGA variants: choose lowest positive height, then bitrate.
     An explicit megaUrl3ds is the fallback when variants are absent/invalid. */
  int variants=get(s,t,n,i,"sources3ds"),best_h=2147483647,best_rate=2147483647;
  if(variants>=0&&t[variants].type==JSMN_ARRAY)for(int v=variants+1;v<n&&t[v].start<t[variants].end;v=skip(t,n,v)){
   if(t[v].type!=JSMN_OBJECT)continue;
   int ht=get(s,t,n,v,"height"),br=get(s,t,n,v,"bitrate");char link[1536]={0};
   if(ht<0||t[ht].type!=JSMN_PRIMITIVE||!string(s,t,get(s,t,n,v,"megaUrl"),link,sizeof(link)))continue;
   int height=atoi(s+t[ht].start),rate=br>=0&&t[br].type==JSMN_PRIMITIVE?atoi(s+t[br].start):2147483647;
   if(height<1||height>4320||strncmp(link,"https://mega.nz/file/",21)||!strchr(link,'#'))continue;
   if(rate<1)rate=2147483647;
   if(height<best_h||(height==best_h&&rate<best_rate)){best_h=height;best_rate=rate;snprintf(e.stream,sizeof(e.stream),"%s",link);}
  }
  int sn=get(s,t,n,i,"seasonNumber"),ep=get(s,t,n,i,"episode");if(sn>=0&&ep>=0&&t[sn].type==JSMN_PRIMITIVE&&t[ep].type==JSMN_PRIMITIVE)snprintf(e.meta,sizeof(e.meta),"Seizoen %d - Aflevering %d",atoi(s+t[sn].start),atoi(s+t[ep].start));
  out[count++]=e;
 }
 free(t);return count;
}
