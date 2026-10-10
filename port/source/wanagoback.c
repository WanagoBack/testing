/* WanagoBack New 3DS prototype. GPL-3.0-or-later; see PORT_README.md. */
#include "wanagoback.h"
#include "3ds.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <curl/curl.h>
#include "system/menu.h"
#include "system/draw/draw.h"
#include "system/util/json.h"
#include "video_player.h"
#define ROOT "/3ds/WanagoBack/"
#define CATALOG "https://raw.githubusercontent.com/WanagoBack/testing/main/library.json"
#define MAX_VIDEOS 512
#define JSON_LIMIT (1024*1024)
typedef struct { char title[192], folder[80], source[1536], url[1536], file[32]; } Entry;
static Entry entries[MAX_VIDEOS], incoming[MAX_VIDEOS];
static int count, incoming_count, selected, page;
static unsigned int ready, done, cancel_job, downloaded, pending_keys;
static unsigned int pending_touch;
static Thread worker;
static int job, job_entry, result;
static char status[180]="Bibliotheek laden...";
static bool first=true;
static Draw_image_data solid;
static void cache_name(Entry* e) {
 uint64_t h=UINT64_C(14695981039346656037);
 const unsigned char* p=(const unsigned char*)(e->source[0]?e->source:e->title);
 for(;*p;p++){h^=*p;h*=UINT64_C(1099511628211);}
 snprintf(e->file,sizeof(e->file),"%016llx.mp4",(unsigned long long)h);
}
static bool field(Json_data* json,int i,const char* name,char* out,size_t capacity) {
 char key[80];Json_extracted_data value={0};
 snprintf(key,sizeof(key),"videos[%d].%s",i,name);
 if(Util_json_get_data(key,json,&value)||value.type!=JSON_TYPE_STRING||!value.value||value.size>=capacity)return false;
 memcpy(out,value.value,value.size);out[value.size]=0;return true;
}
static int parse_catalog(const char* text,Entry* out) {
 Json_data json={0};int n=0;
 if(Util_json_parse(text,&json))return -1;
 for(int i=0;i<MAX_VIDEOS;i++){
  Entry e={0};
  if(!field(&json,i,"title",e.title,sizeof(e.title))||!e.title[0])continue;
  field(&json,i,"folder",e.folder,sizeof(e.folder));
  field(&json,i,"url3ds",e.url,sizeof(e.url));
  if(!field(&json,i,"megaUrl",e.source,sizeof(e.source)))
   if(!field(&json,i,"videoUrl",e.source,sizeof(e.source)))snprintf(e.source,sizeof(e.source),"%s",e.url);
  cache_name(&e);out[n++]=e;
 }
 Util_json_free(&json);return n;
}
typedef struct { char* data;size_t used; } Memory;
static size_t memory_write(char* data,size_t size,size_t nmemb,void* userdata){
 Memory* m=userdata;size_t bytes=size*nmemb;
 if(bytes>JSON_LIMIT-m->used)return 0;
 memcpy(m->data+m->used,data,bytes);m->used+=bytes;m->data[m->used]=0;return bytes;
}
static int progress(void* p,curl_off_t total,curl_off_t current,curl_off_t up_total,curl_off_t up_now){
 (void)p;(void)total;(void)up_total;(void)up_now;
 __atomic_store_n(&downloaded,(unsigned int)(current/1024),__ATOMIC_RELAXED);
 return __atomic_load_n(&cancel_job,__ATOMIC_RELAXED)?1:0;
}
static CURL* connection(const char* url){
 if(strncmp(url,"https://",8))return NULL;
 CURL* c=curl_easy_init();if(!c)return NULL;
 curl_easy_setopt(c,CURLOPT_URL,url);
 curl_easy_setopt(c,CURLOPT_CAINFO,"romfs:/gfx/cert/cacert.pem");
 curl_easy_setopt(c,CURLOPT_SSL_VERIFYPEER,1L);curl_easy_setopt(c,CURLOPT_SSL_VERIFYHOST,2L);
 curl_easy_setopt(c,CURLOPT_PROTOCOLS_STR,"https");curl_easy_setopt(c,CURLOPT_REDIR_PROTOCOLS_STR,"https");
 curl_easy_setopt(c,CURLOPT_FOLLOWLOCATION,1L);curl_easy_setopt(c,CURLOPT_MAXREDIRS,5L);
 curl_easy_setopt(c,CURLOPT_FAILONERROR,1L);curl_easy_setopt(c,CURLOPT_NOSIGNAL,1L);
 curl_easy_setopt(c,CURLOPT_CONNECTTIMEOUT,20L);curl_easy_setopt(c,CURLOPT_LOW_SPEED_LIMIT,128L);
 curl_easy_setopt(c,CURLOPT_LOW_SPEED_TIME,30L);curl_easy_setopt(c,CURLOPT_USERAGENT,"WanagoBack-3DS/0.1");
 curl_easy_setopt(c,CURLOPT_NOPROGRESS,0L);curl_easy_setopt(c,CURLOPT_XFERINFOFUNCTION,progress);
 return c;
}
static size_t disk_write(char* data,size_t size,size_t n,void* file){return fwrite(data,1,size*n,(FILE*)file);}
static void download_worker(void* arg){
 (void)arg;result=-1;
 if(job==1){
  Memory m={calloc(1,JSON_LIMIT+1),0};CURL* c=connection(CATALOG);
  if(m.data&&c){
   curl_easy_setopt(c,CURLOPT_TIMEOUT,60L);curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,memory_write);curl_easy_setopt(c,CURLOPT_WRITEDATA,&m);
   if(curl_easy_perform(c)==CURLE_OK&&(incoming_count=parse_catalog(m.data,incoming))>0){
    FILE* f=fopen(ROOT "library.json.part","wb");
    if(f){bool ok=fwrite(m.data,1,m.used,f)==m.used;if(fclose(f))ok=false;if(ok)rename(ROOT "library.json.part",ROOT "library.json");}
    result=0;
   }
  }
  if(c)curl_easy_cleanup(c);free(m.data);
 }else{
  Entry* e=&entries[job_entry];char temp[176],final[160];
  snprintf(final,sizeof(final),ROOT "videos/%s",e->file);snprintf(temp,sizeof(temp),"%s.part",final);
  CURL* c=connection(e->url);FILE* f=c?fopen(temp,"wb"):NULL;
  if(c&&f){
   curl_easy_setopt(c,CURLOPT_WRITEFUNCTION,disk_write);curl_easy_setopt(c,CURLOPT_WRITEDATA,f);
   CURLcode rc=curl_easy_perform(c);bool ok=!ferror(f);if(fclose(f))ok=false;f=NULL;
   if(rc==CURLE_OK&&ok){
    struct stat info;
    if(!stat(temp,&info)&&info.st_size>0&&rename(temp,final)==0)result=0;
   }
  }
  if(f)fclose(f);if(c)curl_easy_cleanup(c);if(result)remove(temp);
 }
 __atomic_store_n(&done,1,__ATOMIC_RELEASE);
}
static void start_job(int type){
 if(worker)return;job=type;job_entry=selected;result=-1;incoming_count=0;
 __atomic_store_n(&done,0,__ATOMIC_RELAXED);__atomic_store_n(&cancel_job,0,__ATOMIC_RELAXED);__atomic_store_n(&downloaded,0,__ATOMIC_RELAXED);
 worker=threadCreate(download_worker,NULL,128*1024,0x30,-2,false);
 snprintf(status,sizeof(status),"%s",worker?(type==1?"Bibliotheek ophalen...":"Video naar SD downloaden... B = annuleren"):"Kon achtergrondtaak niet starten.");
}
void Wgb_init(void){
 mkdir("/3ds",0777);mkdir(ROOT,0777);mkdir(ROOT "videos",0777);
 solid=Draw_get_empty_image();
 FILE* f=fopen(ROOT "library.json","rb");
 if(f){char* text=calloc(1,JSON_LIMIT+1);if(text){size_t n=fread(text,1,JSON_LIMIT,f);text[n]=0;int loaded=parse_catalog(text,entries);if(loaded>0)count=loaded;free(text);}fclose(f);}
 __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
}
bool Wgb_ready(void){return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=0;}
void Wgb_exit(void){
 __atomic_store_n(&ready,0,__ATOMIC_RELEASE);__atomic_store_n(&cancel_job,1,__ATOMIC_RELAXED);
 if(worker){threadJoin(worker,UINT64_MAX);threadFree(worker);worker=NULL;}
}
void Wgb_hid(const Hid_info* key){
 unsigned int bits=0;
 if(DEF_HID_PHY_PR(key->a))bits|=1;if(DEF_HID_PHY_PR(key->b))bits|=2;
 if(DEF_HID_PHY_PR(key->d_up))bits|=4;if(DEF_HID_PHY_PR(key->d_down))bits|=8;
 if(DEF_HID_PHY_PR(key->x))bits|=16;if(DEF_HID_PHY_PR(key->y))bits|=32;
 if(DEF_HID_PHY_PR(key->start))bits|=64;
 if(DEF_HID_PHY_PR(key->touch)&&key->touch_y>=44&&key->touch_y<194)
  __atomic_store_n(&pending_touch,(unsigned int)((key->touch_y-44)/30+1),__ATOMIC_RELEASE);
 __atomic_fetch_or(&pending_keys,bits,__ATOMIC_RELEASE);
}
static void play_selected(void){
 if(!count)return;Entry* e=&entries[selected];char path[160];struct stat info;
 snprintf(path,sizeof(path),ROOT "videos/%s",e->file);
 if(!stat(path,&info)&&info.st_size>0){
  if(!Vid_query_init_flag())Vid_init(true);else Vid_resume();
  if(!Vid_wgb_open(e->file,ROOT "videos/")){Menu_resume();snprintf(status,sizeof(status),"Video kon niet worden geopend.");}
 }else if(e->url[0])start_job(2);
 else snprintf(status,sizeof(status),"Zet deze video op SD met prepare_sd.py.");
}
static void rect(float x,float y,float w,float h,uint32_t color){Draw_texture(&solid,color,x,y,w,h);}
void Wgb_main(void){
 if(first){first=false;start_job(1);}
 if(worker&&__atomic_load_n(&done,__ATOMIC_ACQUIRE)){
  threadJoin(worker,UINT64_MAX);threadFree(worker);worker=NULL;
  if(!result){
   if(job==1){count=incoming_count;memcpy(entries,incoming,sizeof(Entry)*(size_t)count);selected=0;snprintf(status,sizeof(status),"%d video's. Kies een titel.",count);}
   else{snprintf(status,sizeof(status),"Download klaar. A = afspelen.");}
  }else snprintf(status,sizeof(status),"%s",job==1?"Ophalen mislukt. SD-bibliotheek blijft beschikbaar.":"Download mislukt of geannuleerd. Probeer opnieuw.");
 }
 unsigned int keys=__atomic_exchange_n(&pending_keys,0,__ATOMIC_ACQUIRE);
 unsigned int touch=__atomic_exchange_n(&pending_touch,0,__ATOMIC_ACQUIRE);
 if(worker){if(keys&2)__atomic_store_n(&cancel_job,1,__ATOMIC_RELAXED);}
 else{
  if((keys&4)&&selected>0)selected--;if((keys&8)&&selected+1<count)selected++;
  if(touch&&page*5+(int)touch-1<count){selected=page*5+(int)touch-1;keys|=1;}
  if(keys&16)start_job(1);
  if(keys&64){Menu_set_must_exit_flag(true);return;}
  if(keys&32){if(!Vid_query_init_flag())Vid_init(true);else Vid_resume();return;}
  if(keys&1){play_selected();if(Vid_query_running_flag())return;}
 }
 page=selected/5;
 Draw_frame_ready();Draw_screen_ready(DRAW_SCREEN_TOP_LEFT,0xffe8ba68);
 rect(0,0,400,53,0xff0eb8ff);Draw_c("WanagoBack",24,13,1.1f,0xff242424);
 rect(18,71,364,107,0xfff6f6f6);
 Draw_c(count?entries[selected].folder:"Welkom op New 3DS",29,82,.55f,0xff8c5923);
 char title[64];snprintf(title,sizeof(title),"%.52s",count?entries[selected].title:"Je bibliotheek verschijnt hier");
 Draw_c(title,29,111,.55f,0xff222222);
 Draw_c(worker?"Even geduld...":"A / tik: kies video",29,147,.5f,0xff222222);
 Draw_c(status,12,191,.38f,0xff222222);
 if(worker){char p[64];snprintf(p,sizeof(p),"Ontvangen: %u KiB",__atomic_load_n(&downloaded,__ATOMIC_RELAXED));Draw_c(p,12,216,.4f,0xff222222);}
 Draw_screen_ready(DRAW_SCREEN_BOTTOM,0xffeee1bf);rect(0,0,320,36,0xff0eb8ff);Draw_c("Jouw video's",12,7,.65f,0xff222222);
 for(int row=0;row<5;row++){
  int i=page*5+row;if(i>=count)break;
  rect(8,44+row*30,304,27,i==selected?0xff0eb8ff:0xffffffff);
  char line[64];snprintf(line,sizeof(line),"%.43s",entries[i].title);Draw_c(line,15,49+row*30,.45f,0xff222222);
 }
 Draw_c("X: verversen   Y: SD-player   START: sluiten",8,211,.37f,0xff222222);
 Draw_apply_draw();
}
