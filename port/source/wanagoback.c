/* WanagoBack New 3DS prototype. GPL-3.0-or-later; see PORT_README.md. */
#include "wanagoback.h"
#include "wgb_skin.h"
#include "wgb_mega.h"
#include "wgb_storage.h"
#include "wgb_catalog.h"
#include "wgb_music.h"
#include "wgb_lists.h"
#include <errno.h>
#include <math.h>
#include <ctype.h>
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
#define ROOT "sdmc:/3ds/WanagoBack/"
#define CATALOG "https://raw.githubusercontent.com/WanagoBack/testing/main/library.json"
#define MAX_VIDEOS 512
#define JSON_LIMIT (1024*1024)

static Entry entries[MAX_VIDEOS], incoming[MAX_VIDEOS];
static int count, incoming_count, selected;
static unsigned int ready, done, cancel_job, downloaded, pending_keys;
static unsigned int pending_touch;
static Thread worker;
static int job, job_entry, result;
static char status[180]="Bibliotheek laden...";
static char catalog_status[140]="Ingebouwde videolijst";
static int storage_errno;
static bool first=true;
typedef struct {char name[49];uint32_t id;uint8_t avatar;} WgbProfile;
static WgbProfile profiles[4],editing;
static int profile_count,active_profile,edit_index=-1;
static uint32_t next_profile_id=2;
static const int avatar_images[]={WGB_AV_100,WGB_AV_101,WGB_AV_201,WGB_AV_300,WGB_AV_301,WGB_AV_401,WGB_AV_501,WGB_AV_600,WGB_AV_601,WGB_AV_800,WGB_AV_801,WGB_AV_0000};
static bool save_profiles(void){
 FILE* f=fopen(ROOT "profiles.dat.part","wb");if(!f){storage_errno=errno;return false;}
 uint32_t h[4]={0x57475033,(uint32_t)profile_count,(uint32_t)active_profile,next_profile_id};
 bool ok=fwrite(h,sizeof(h),1,f)==1&&fwrite(profiles,sizeof(profiles),1,f)==1;
 if(fclose(f))ok=false;if(ok)ok=Wstore_replace(ROOT "profiles.dat.part",ROOT "profiles.dat");storage_errno=ok?0:errno;return ok;
}
static void load_profiles(void){
 uint32_t h[4]={0};bool ok=false;const char* files[]={ROOT "profiles.dat",ROOT "profiles.dat.bak"};
 for(int j=0;j<2&&!ok;j++){
  FILE* f=fopen(files[j],"rb");if(!f)continue;
  ok=fread(h,sizeof(h),1,f)==1&&fread(profiles,sizeof(profiles),1,f)==1;fclose(f);
  ok=ok&&h[0]==0x57475033&&h[1]>0&&h[1]<=4&&h[2]<h[1]&&h[3]>1;
  if(ok)for(unsigned i=0;i<h[1];i++)if(!memchr(profiles[i].name,0,49)||!profiles[i].name[0]||profiles[i].avatar>=12||!profiles[i].id||profiles[i].id>=h[3])ok=false;
 }
 if(ok){profile_count=(int)h[1];active_profile=(int)h[2];next_profile_id=h[3];}
 else{memset(profiles,0,sizeof(profiles));strcpy(profiles[0].name,"Samson");profiles[0].avatar=8;profiles[0].id=1;profile_count=1;active_profile=0;save_profiles();}
}
static void profile_mark_path(char* out,size_t n,const Entry* e,const char* kind){
 unsigned id=profiles[active_profile].id;
 if(id==1){snprintf(out,n,ROOT "%s/%s",kind,e->file);return;}
 snprintf(out,n,ROOT "profile_%u/%s/%s",id,kind,e->file);
}

static WgbLists collections,lists_backup;
static unsigned lists_profile;
static int active_list,list_page;
static bool list_picker;
static int list_edit_index=-1,list_edit_icon=1;
static char list_edit_name[49];
static void lists_path(char* path,size_t n){snprintf(path,n,ROOT "collections_%u.dat",profiles[active_profile].id);}
static void load_lists(void){
 if(lists_profile==profiles[active_profile].id)return;
 char path[200];lists_path(path,sizeof(path));
 if(!Wlists_load(path,&collections)){
  /* One-time migration of earlier per-video markers. */
  for(int i=0;i<count;i++){struct stat st;profile_mark_path(path,sizeof(path),&entries[i],"favorites");if(!stat(path,&st)&&!Wlists_has(&collections.favorites,entries[i].file))Wlists_toggle(&collections.favorites,entries[i].file);
   profile_mark_path(path,sizeof(path),&entries[i],"playlist");if(!stat(path,&st)&&!Wlists_has(&collections.lists[0],entries[i].file))Wlists_toggle(&collections.lists[0],entries[i].file);
  }
 }
 lists_profile=profiles[active_profile].id;active_list=0;list_page=0;
}
static bool save_lists(void){char path[200];lists_path(path,sizeof(path));if(Wlists_save(path,&collections))return true;int cause=errno;collections=lists_backup;snprintf(status,sizeof(status),"Opslaan mislukt (SD-fout %d).",cause);return false;}


static void cache_name(Entry* e) {
 uint64_t h=UINT64_C(14695981039346656037);
 const unsigned char* p=(const unsigned char*)(e->source[0]?e->source:e->title);
 for(;*p;p++){h^=*p;h*=UINT64_C(1099511628211);}
 snprintf(e->file,sizeof(e->file),"%016llx.mp4",(unsigned long long)h);
}
static int parse_catalog(const char* text,Entry* out){int n=Wcatalog_parse(text,out,MAX_VIDEOS);for(int i=0;i<n;i++)cache_name(&out[i]);return n;}
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
 curl_easy_setopt(c,CURLOPT_URL,url);curl_easy_setopt(c,CURLOPT_HTTP_VERSION,CURL_HTTP_VERSION_1_1);
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
   CURLcode rc=curl_easy_perform(c);long http=0;curl_easy_getinfo(c,CURLINFO_RESPONSE_CODE,&http);
   snprintf(catalog_status,sizeof(catalog_status),"Offline lijst: netwerk %d / HTTP %ld",(int)rc,http);
   if(rc==CURLE_OK&&(incoming_count=parse_catalog(m.data,incoming))>0){
    snprintf(catalog_status,sizeof(catalog_status),"Online bibliotheek: %d video's",incoming_count);
    FILE* f=fopen(ROOT "library.json.part","wb");
    if(f){bool ok=fwrite(m.data,1,m.used,f)==m.used;if(fclose(f))ok=false;if(ok)Wstore_replace(ROOT "library.json.part",ROOT "library.json");}
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
    if(!stat(temp,&info)&&info.st_size>0&&Wstore_replace(temp,final))result=0;
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
 struct stat sd; if(stat("sdmc:/",&sd)!=0)archiveMountSdmc();
 mkdir("sdmc:/3ds",0777);mkdir(ROOT,0777);mkdir(ROOT "videos",0777);
 curl_global_init(CURL_GLOBAL_DEFAULT);
 Wskin_init();load_profiles();
 mkdir(ROOT "music",0777);mkdir(ROOT "favorites",0777);mkdir(ROOT "playlist",0777);mkdir(ROOT "thumbs",0777);
 const char* sources[]={ROOT "library.json",ROOT "library.json.bak","romfs:/wgb/library.json"};
 for(int i=0;i<3&&!count;i++){FILE* f=fopen(sources[i],"rb");
  if(f){char* text=calloc(1,JSON_LIMIT+1);if(text){size_t n=fread(text,1,JSON_LIMIT,f);text[n]=0;int loaded=parse_catalog(text,entries);if(loaded>0)count=loaded;free(text);}fclose(f);}
 }
 __atomic_store_n(&ready,1,__ATOMIC_RELEASE);
}
bool Wgb_ready(void){return __atomic_load_n(&ready,__ATOMIC_ACQUIRE)!=0;}
void Wgb_exit(void){
 Wmega_cancel();
 __atomic_store_n(&ready,0,__ATOMIC_RELEASE);__atomic_store_n(&cancel_job,1,__ATOMIC_RELAXED);
 if(worker){threadJoin(worker,UINT64_MAX);threadFree(worker);worker=NULL;}
 Wmusic_exit();Wskin_exit();
}
void Wgb_hid(const Hid_info* key){
 unsigned int bits=0;
 if(DEF_HID_PHY_PR(key->a))bits|=1;if(DEF_HID_PHY_PR(key->b))bits|=2;
 if(DEF_HID_PHY_PR(key->d_up))bits|=4;if(DEF_HID_PHY_PR(key->d_down))bits|=8;
 if(DEF_HID_PHY_PR(key->x))bits|=16;if(DEF_HID_PHY_PR(key->start))bits|=64;
 if(DEF_HID_PHY_PR(key->d_left))bits|=128;if(DEF_HID_PHY_PR(key->d_right))bits|=256;
 if(DEF_HID_PHY_RE(key->touch)){
  int dx=key->touch_x_last-key->touch_x_initial,dy=key->touch_y_last-key->touch_y_initial;
  unsigned int gesture=0;
  if(abs(dx)>24||abs(dy)>24)gesture=abs(dx)>abs(dy)?(dx<0?1:2):(dy<0?3:4);
  unsigned int packet=(unsigned int)key->touch_x_initial|((unsigned int)key->touch_y_initial<<9)|(1u<<18)|(gesture<<19);
  __atomic_store_n(&pending_touch,packet,__ATOMIC_RELEASE);
 }
 __atomic_fetch_or(&pending_keys,bits,__ATOMIC_RELEASE);
}
void Wgb_video_stage(const char* stage){
 FILE* f=fopen(ROOT "last_playback.txt","w");if(!f)return;
 fprintf(f,"WanagoBack 0.5\nVideo: %s\nStap: %s\n",count?entries[selected].title:"",stage);fclose(f);
}
static void play_selected(void){
 if(!count)return;Wgb_video_stage("Afspelen aangevraagd");Entry* e=&entries[selected];char path[160];struct stat info;
 snprintf(path,sizeof(path),ROOT "videos/%s",e->file);
 if(!stat(path,&info)&&info.st_size>0){
  Wmusic_stop();Wmega_begin();
  if(!Vid_query_init_flag())Vid_init(true);else Vid_resume();
  if(!Vid_wgb_open(e->file,ROOT "videos/")){Menu_resume();snprintf(status,sizeof(status),"Video kon niet worden geopend.");}
 }else if(e->url[0]&&!e->stream[0])start_job(2);
 else if(e->stream[0]||(e->source[0]&&strstr(e->source,"mega."))){
  char stream_path[96];
  if(!Wmega_path(e->stream[0]?e->stream:e->source,stream_path,sizeof(stream_path))){snprintf(status,sizeof(status),"%s",Wmega_error());return;}
  Wmusic_stop();Wmega_begin();
  if(!Vid_query_init_flag())Vid_init(true);else Vid_resume();
  if(!Vid_wgb_open(stream_path,"")){Menu_resume();snprintf(status,sizeof(status),"Video kon niet worden geopend.");}
 }else if(e->url[0])start_job(2);
 else snprintf(status,sizeof(status),"Zet deze video op SD met prepare_sd.py.");
}
/* Windows layout: top navigation, character carousel, genre rows and yellow modal. */
typedef struct {float x,y,w,h;int action,value;} Hit;
static Hit hits[80];static int hit_count;
static int screen=7,home_index=0,group_count=0,row_page=0;
static char groups[64][80],filter[80];
static int grouped[5][MAX_VIDEOS],group_sizes[5],offsets[5];
static float visual_offsets[5];
static uint64_t scene_time,loading_time;
static bool modal=false;
static int return_screen=1;
static int pending_action=-1,pending_value;static uint64_t pressed_at;
static const int category_icons[]={WGB_SERIES,WGB_FILM,WGB_KARAOKE,WGB_MUSIC,WGB_SERIES};
static const uint32_t category_colors[]={0xff9b29c5,0xff0015d3,0xffede5dd,0xff00d797,0xff2ab043};
static void hit(float x,float y,float w,float h,int action,int value){if(hit_count<80)hits[hit_count++]=(Hit){x,y,w,h,action,value};}
static float elapsed(uint64_t start){return (float)(osGetTime()-start)/1000.0f;}
typedef struct {int screen,home,row,offset[5];} Nav;
static Nav history[16];static int history_count;
static void rebuild(void);
static void set_screen(int next){screen=next;modal=false;row_page=0;pending_action=-1;hit_count=0;scene_time=osGetTime();}
static void change_screen(int next){
 if(screen!=next){if(history_count==16){memmove(history,history+1,15*sizeof(Nav));history_count--;}
 Nav* n=&history[history_count++];n->screen=screen;n->home=home_index;n->row=row_page;memcpy(n->offset,offsets,sizeof(offsets));}
 set_screen(next);
}
static void go_back(void){
 pending_action=-1;__atomic_store_n(&pending_touch,0,__ATOMIC_RELAXED);
 if(modal){modal=false;screen=return_screen;hit_count=0;rebuild();return;}
 if(history_count){Nav n=history[--history_count];set_screen(n.screen);home_index=n.home;row_page=n.row;memcpy(offsets,n.offset,sizeof(offsets));for(int i=0;i<5;i++)visual_offsets[i]=(float)offsets[i];rebuild();}
 else if(screen!=7){set_screen(7);rebuild();}
}
static int category(const Entry* e){
 if(strstr(e->category,"karaoke"))return 2;
 if(strstr(e->category,"film"))return 1;
 if(strstr(e->category,"clip")||strstr(e->category,"music"))return 3;
 if(strstr(e->category,"special"))return 4;
 return 0;
}
static bool marked(const Entry* e,const char* kind){load_lists();return Wlists_has(!strcmp(kind,"favorites")?&collections.favorites:&collections.lists[active_list],e->file);}
static void toggle(const Entry* e,const char* kind){
 load_lists();lists_backup=collections;WgbList* list=!strcmp(kind,"favorites")?&collections.favorites:&collections.lists[active_list];
 bool existed=Wlists_has(list,e->file);
 if(!Wlists_toggle(list,e->file)){snprintf(status,sizeof(status),"Deze lijst is vol.");return;}
 if(save_lists())snprintf(status,sizeof(status),"%s %s",existed?"Verwijderd uit":"Toegevoegd aan",!strcmp(kind,"favorites")?"favorieten":list->name);
}
static bool matches(const char* title,const char* needle){
 if(!*needle)return true;
 for(;*title;title++){const unsigned char*a=(const unsigned char*)title,*b=(const unsigned char*)needle;
  while(*a&&*b&&tolower(*a)==tolower(*b)){a++;b++;}if(!*b)return true;
 }return false;
}
static void rebuild(void){
 load_lists();
 static const char* all[]={"SamsonGert","Plop","FredSamson","MegaMindy","K3","Heidi","InDeKijker","Rox","PietPiraat","Maya","Prinsessia","Bumba","Wickie"};
 group_count=13;for(int i=0;i<13;i++)snprintf(groups[i],80,"%s",all[i]);memset(group_sizes,0,sizeof(group_sizes));
 for(int i=0;i<count;i++){
  int g=0;for(;g<group_count;g++)if(!strcmp(groups[g],entries[i].folder))break;
  if(!strcmp(entries[i].folder,"FeaturedItems"))continue;
  if(g==group_count&&group_count<64)snprintf(groups[group_count++],80,"%s",entries[i].folder);
 }
 if(home_index>=group_count)home_index=0;
 for(int i=0;i<count;i++){
  bool use=screen==3?marked(&entries[i],"favorites"):screen==4?marked(&entries[i],"playlist"):
    screen==6?matches(entries[i].title,filter):(group_count&&(!strcmp(groups[home_index],entries[i].folder)||(!strcmp(groups[home_index],"InDeKijker")&&!strcmp(entries[i].folder,"FeaturedItems"))));
  if(use){int c=screen==4?0:category(&entries[i]);grouped[c][group_sizes[c]++]=i;}
 }
 if(screen==4){group_sizes[0]=0;WgbList* l=&collections.lists[active_list];for(unsigned j=0;j<l->count;j++)for(int i=0;i<count;i++)if(!strcmp(l->keys[j],entries[i].file)){grouped[0][group_sizes[0]++]=i;break;}}
 for(int c=0;c<5;c++){int max=group_sizes[c]>2?group_sizes[c]-2:0;if(offsets[c]>max)offsets[c]=max;if(offsets[c]<0)offsets[c]=0;}
}
static const char* extra_characters[]={"FredSamson","MegaMindy","K3","Heidi","Rox","PietPiraat","Maya","Prinsessia","Bumba","Wickie"};
static int character_image(const char* folder){if(!strcmp(folder,"Plop"))return WGB_PLOP;if(!strcmp(folder,"SamsonGert"))return WGB_SAMSON;for(int i=0;i<10;i++)if(!strcmp(folder,extra_characters[i]))return WGB_CHAR_FREDSAMSON+i;return WGB_FEATURED;}
static const char* character_name(const char* folder){if(!strcmp(folder,"InDeKijker"))return "In de kijker";if(!strcmp(folder,"SamsonGert"))return "Samson en Gert";if(!strcmp(folder,"Plop"))return "Kabouter Plop";if(!strcmp(folder,"FredSamson"))return "Fred en Samson";if(!strcmp(folder,"MegaMindy"))return "Mega Mindy";if(!strcmp(folder,"PietPiraat"))return "Piet Piraat";return folder;}
static void icon(int image,float x,float y,float size,int action,int value){float inset=(pending_action==action&&pending_value==value)?size*.04f:0;Wskin_image(image,x+inset,y+inset,size-2*inset,size-2*inset);hit(x-2,y-2,size+4,size+4,action,value);}
static void navbar(void){
 Wskin_rect(0,0,320,30,0xcfffffff);
 icon(WGB_BACK,3,3,24,1,0);icon(avatar_images[profiles[active_profile].avatar],33,3,24,17,0);icon(WGB_HOME,63,3,24,2,0);
 icon(WGB_FAVORITES,218,3,24,3,0);icon(WGB_SEARCH,251,3,24,4,0);icon(WGB_SETTINGS,284,3,24,5,0);
}
static void text_center(const char* s,float x,float y,float w,float size,uint32_t color){float tw=Wskin_width(s,size);Wskin_text(s,x+fmaxf(0,(w-tw)/2),y,size,color,w,1);}
static float bounce(void){float t=fminf(1,elapsed(scene_time)/.42f);return t<.65f?(.9f+.125f*(t/.65f)):(1.025f-.025f*((t-.65f)/.35f));}
static void draw_home(void){
 text_center("KIES EEN PERSONAGE",30,50,260,14,0xff242424);
 float scale=bounce();
 for(int col=0;col<2;col++){
  int i=home_index+col;if(i>=group_count)break;
  float w=112*scale,h=112*scale,x=38+col*142+(112-w)/2,y=82+(112-h)/2;
  Wskin_image(character_image(groups[i]),x,y,w,h);text_center(character_name(groups[i]),x,200,w,9,0xff242424);hit(x,y,w,h,6,i);
 }
 if(home_index>0)icon(WGB_LEFT,0,121,25,7,-1);
 if(home_index+2<group_count)icon(WGB_RIGHT,295,121,25,7,1);
 if(!group_count)text_center("Geen video's gevonden",30,115,260,10,0xff242424);
}
static void draw_browse(void){
 int row=0,visible=0;
 for(int c=0;c<5;c++)if(group_sizes[c]){
  if(row++<row_page)continue;if(visible==2)break;
  float y=36+visible*91;visible++;
  Wskin_rect(0,y,25,87,category_colors[c]);Wskin_image(category_icons[c],3,y+56,19,19);
  Wskin_image(WGB_LABEL_SERIES+c,8,y+7,8,43);
  visual_offsets[c]+=(offsets[c]*109-visual_offsets[c])*.23f;

  for(int j=0;j<group_sizes[c];j++){
   float x=36+j*109-visual_offsets[c];if(x>320||x+97<26)continue;
   int i=grouped[c][j];float rise=fmaxf(0,1-elapsed(scene_time)/.3f)*8;
   Wskin_round(x,y+rise,97,79,4,0xffffffff);Wskin_thumb(entries[i].file,x,y+rise,97,53);
   text_center(entries[i].title,x+4,y+56+rise,89,6.5f,0xff222222);
   text_center("Tik hier!",x+4,y+69+rise,83,6,0xff222222);Wskin_image(category_icons[c],x+81,y+64+rise,13,13);
   float hx=fmaxf(26,x);hit(hx,y,fminf(320,x+97)-hx,79,8,i);
  }
  Wskin_rect(0,y,25,87,category_colors[c]);Wskin_image(category_icons[c],3,y+56,19,19);
  Wskin_image(WGB_LABEL_SERIES+c,8,y+7,8,43);
 }
 if(!visible)text_center("Hier staan nog geen video's",30,95,270,10,0xff242424);
 icon(WGB_LEFT,111,217,20,9,-1);icon(WGB_RIGHT,190,217,20,9,1);
 text_center(screen==3?"Favorieten":screen==4?collections.lists[active_list].name:screen==6?"Zoeken":(group_count?character_name(groups[home_index]):"Video's"),8,221,96,7,0xff242424);
}
static void draw_modal(void){
 icon(WGB_BACK,3,3,26,1,0);
 Entry* e=&entries[selected];Wskin_rect(0,0,320,240,0x99000000);
 float scale=bounce(),w=296*scale,h=142*scale,x=(320-w)/2,y=(240-h)/2;
 Wskin_image(WGB_PANEL,x,y,w,h);
 icon(WGB_CLOSE,x+w-20,y+3,19,10,0);
 Wskin_thumb(e->file,x+16,y+21,99,61);
 Wskin_text(e->title,x+124,y+21,9,0xff202020,w-141,2);
 Wskin_text(e->description,x+124,y+48,7,0xff303030,w-141,3);
 Wskin_text(e->meta,x+124,y+82,6,0xff726231,w-141,1);
 icon(WGB_PLAY,99,155,29,11,0);
 if(marked(e,"favorites"))Wskin_round(142,154,31,31,5,0x99ffffff);
 icon(WGB_FAVORITES,143,155,29,12,0);if(marked(e,"playlist"))Wskin_round(186,154,31,31,5,0x99ffffff);icon(WGB_PLAYLIST,187,155,29,13,0);
 if(status[0])Wskin_text(status,18,216,7,0xff202020,284,2);
}
static void text_button(const char* label,float x,float y,float w,int action,int value);
static void settings(void){
 Wskin_text(catalog_status,18,216,7,0xff222222,284,2);
 Wskin_image(WGB_PANEL,15,55,290,151);text_center("INSTELLINGEN",35,69,250,13,0xff222222);
 icon(WGB_REFRESH,46,108,40,14,0);Wskin_text("Verversen",37,152,8,0xff222222,70,1);

 icon(WGB_CLOSE,233,108,40,16,0);Wskin_text("Afsluiten",227,152,8,0xff222222,70,1);
 text_button(Wmusic_status(),40,174,240,28,0);
}
static void text_button(const char* label,float x,float y,float w,int action,int value){Wskin_round(x,y,w,23,5,0xff00ccff);text_center(label,x+3,y+6,w-6,8,0xff222222);hit(x,y,w,23,action,value);}
static void draw_lists(void){
 text_center(list_picker?"KIES EEN PLAYLIST":"MIJN PLAYLISTS",35,35,250,13,0xff222222);
 for(int j=0;j<4;j++){int i=list_page*4+j;if(i>=(int)collections.count)break;float y=57+j*32;
  char label[90];snprintf(label,sizeof(label),"%s%s (%u)",list_picker&&Wlists_has(&collections.lists[i],entries[selected].file)?"* ":"",collections.lists[i].name,collections.lists[i].count);
  Wskin_image(WGB_PL_FRAME_1+(int)collections.lists[i].icon-1,15,y,28,28);text_button(label,49,y,list_picker?256:178,30,i);
  if(!list_picker){text_button("Naam",230,y,40,32,i);text_button("X",276,y,28,33,i);}
 }
 text_button("NIEUWE PLAYLIST",56,188,208,31,0);
 if(collections.count>4)text_button(list_page?"VORIGE":"VOLGENDE",100,215,120,34,0);
 if(status[0])Wskin_text(status,16,177,6,0xff222222,288,1);
}
static void playlist_name(int index){
 if(index<0&&collections.count==WLIST_MAX){snprintf(status,sizeof(status),"Maximaal 8 playlists.");return;}
 list_edit_index=index;list_edit_icon=index<0?1:(int)collections.lists[index].icon;
 snprintf(list_edit_name,sizeof(list_edit_name),"%s",index<0?"":collections.lists[index].name);status[0]=0;change_screen(13);
}
static void draw_list_editor(void){
 icon(WGB_BACK,3,3,24,1,0);text_center("KIES EEN PLAATJE",35,13,270,13,0xff222222);
 text_button(list_edit_name[0]?list_edit_name:"Tik hier voor een naam",45,39,230,37,0);
 for(int i=0;i<15;i++){float x=29+(i%5)*54,y=72+(i/5)*44;
  if(list_edit_icon==i+1)Wskin_round(x-2,y-2,42,42,5,0xddffffff);
  icon(WGB_PL_ICON_1+i,x,y,38,36,i+1);
 }
 text_button("ANNULEREN",36,211,113,1,0);text_button("OPSLAAN",171,211,113,38,0);
 if(status[0])Wskin_text(status,18,202,6,0xff222222,284,1);
}
static void draw_collection_home(void){
 text_center("FAVORIETEN & PLAYLISTS",30,35,260,12,0xff222222);
 Wskin_text("Favorieten",15,55,9,0xff222222,120,1);text_button("ALLES",239,50,64,39,0);
 int shown=0;for(int i=0;i<count&&shown<3;i++)if(marked(&entries[i],"favorites")){
  float x=15+shown*101;Wskin_thumb(entries[i].file,x,78,88,45);hit(x,78,88,45,8,i);shown++;
 }
 if(!shown)text_center("Nog geen favorieten",15,91,290,9,0xff222222);
 Wskin_text("Playlists",15,133,9,0xff222222,100,1);text_button("NIEUWE PLAYLIST",156,128,148,31,0);
 for(int j=0;j<3;j++){int i=list_page*3+j;if(i>=(int)collections.count)break;float x=17+j*101;
  Wskin_image(WGB_PL_FRAME_1+(int)collections.lists[i].icon-1,x+17,157,49,49);hit(x,155,85,60,30,i);
  text_center(collections.lists[i].name,x,207,85,6.5f,0xff222222);
  icon(WGB_SETTINGS,x+66,158,16,32,i);icon(WGB_CLOSE,x+66,179,16,33,i);
 }
 if(collections.count>3){text_button("<",9,217,29,40,-1);text_button(">",281,217,29,40,1);}
 if(status[0])Wskin_text(status,43,225,6,0xff222222,230,1);
}
static void draw_profiles(void){
 text_center("KIES EEN PROFIEL",15,13,290,16,0xff242424);
 if(history_count)icon(WGB_BACK,3,3,25,1,0);
 for(int i=0;i<4;i++){float x=38+(i%2)*144,y=44+(i/2)*96;
  Wskin_round(x,y,102,87,8,0xddffffff);
  if(i<profile_count){Wskin_image(avatar_images[profiles[i].avatar],x+19,y+3,64,64);text_center(profiles[i].name,x+4,y+69,94,9,0xff222222);hit(x,y,102,87,20,i);icon(WGB_SETTINGS,x+80,y+3,17,21,i);}
  else{Wskin_image(WGB_ADD_USER,x+28,y+8,46,46);text_center("NIEUW PROFIEL",x+3,y+64,96,8,0xff222222);hit(x,y,102,87,22,0);}
 }
}
static void draw_profile_editor(void){
 if(!strncmp(status,"Profiel opslaan",14))text_center(status,8,173,304,6,0xff222222);
 icon(WGB_BACK,3,3,26,1,0);text_center(edit_index<0?"NIEUW PROFIEL":"PROFIEL AANPASSEN",32,13,280,13,0xff222222);
 icon(WGB_PROFILE_LEFT,52,80,30,23,-1);Wskin_image(avatar_images[editing.avatar],112,44,96,96);icon(WGB_PROFILE_RIGHT,238,80,30,23,1);
 Wskin_round(70,146,180,26,5,0xeeffffff);text_center(editing.name[0]?editing.name:"Tik om je naam in te vullen",75,154,170,9,0xff222222);hit(70,146,180,26,24,0);
 text_button("ANNULEREN",39,186,109,1,0);text_button("OPSLAAN",172,186,109,25,0);
 if(edit_index>=0&&profile_count>1)text_button("VERWIJDEREN",106,214,108,26,0);
}
static void edit_profile(int i){edit_index=i;memset(&editing,0,sizeof(editing));if(i>=0)editing=profiles[i];else{editing.avatar=8;editing.id=next_profile_id;}change_screen(8);}
static void choose_profile_name(void){SwkbdState k;swkbdInit(&k,SWKBD_TYPE_NORMAL,2,12);swkbdSetHintText(&k,"Naam van je profiel");swkbdSetInitialText(&k,editing.name);swkbdSetValidation(&k,SWKBD_NOTEMPTY_NOTBLANK,0,0);char name[49]={0};if(swkbdInputText(&k,name,sizeof(name))==SWKBD_BUTTON_CONFIRM)snprintf(editing.name,sizeof(editing.name),"%s",name);}
static void dispatch(int action,int value){
 switch(action){
 case 1:go_back();break;
 case 2:change_screen(0);break;
 case 3:list_picker=false;list_page=0;status[0]=0;change_screen(12);rebuild();break;
 case 4:{SwkbdState keyboard;swkbdInit(&keyboard,SWKBD_TYPE_NORMAL,2,79);swkbdSetHintText(&keyboard,"Zoek een video");if(swkbdInputText(&keyboard,filter,sizeof(filter))==SWKBD_BUTTON_CONFIRM){change_screen(6);rebuild();}break;}
 case 5:change_screen(5);break;
 case 6:change_screen(1);home_index=value;memset(offsets,0,sizeof(offsets));memset(visual_offsets,0,sizeof(visual_offsets));rebuild();break;
 case 7:if(!group_count)break;home_index+=value;if(home_index<0)home_index=0;if(home_index>=group_count)home_index=group_count-1;scene_time=osGetTime();break;
 case 8:selected=value;return_screen=screen;modal=true;scene_time=osGetTime();break;
 case 9:if(value<0){if(row_page>0)row_page--;}else{int n=0;for(int i=0;i<5;i++)if(group_sizes[i])n++;if(row_page+2<n)row_page++;}break;
 case 10:modal=false;screen=return_screen;rebuild();break;
 case 11:if(worker){__atomic_store_n(&cancel_job,1,__ATOMIC_RELAXED);snprintf(status,sizeof(status),"Even wachten tot verversen gestopt is.");}else{loading_time=osGetTime();play_selected();}break;
 case 12:toggle(&entries[selected],"favorites");break;
 case 13:list_picker=true;list_page=0;status[0]=0;change_screen(10);break;
 case 14:start_job(1);break;
 case 15:list_picker=false;list_page=0;status[0]=0;change_screen(12);break;
 case 16:Menu_set_must_exit_flag(true);break;
 case 28:Wmusic_toggle();break;
 case 30:if(value>=0&&value<(int)collections.count){active_list=value;if(list_picker){toggle(&entries[selected],"playlist");}else{memset(offsets,0,sizeof(offsets));memset(visual_offsets,0,sizeof(visual_offsets));change_screen(4);rebuild();}}break;
 case 31:playlist_name(-1);break;
 case 32:playlist_name(value);break;
 case 33:active_list=value;change_screen(11);break;
 case 34:list_page=!list_page;break;
 case 36:list_edit_icon=value;break;
 case 37:{SwkbdState k;swkbdInit(&k,SWKBD_TYPE_NORMAL,2,20);swkbdSetHintText(&k,"Naam van je playlist");swkbdSetInitialText(&k,list_edit_name);swkbdSetValidation(&k,SWKBD_NOTEMPTY_NOTBLANK,0,0);char name[49]={0};if(swkbdInputText(&k,name,sizeof(name))==SWKBD_BUTTON_CONFIRM)snprintf(list_edit_name,49,"%s",name);break;}
 case 38:{if(!list_edit_name[0]){snprintf(status,sizeof(status),"Vul eerst een naam in.");break;}lists_backup=collections;int i=list_edit_index;
  if(i<0){if(collections.count>=WLIST_MAX)break;i=(int)collections.count++;memset(&collections.lists[i],0,sizeof(WgbList));}
  snprintf(collections.lists[i].name,49,"%s",list_edit_name);collections.lists[i].icon=(unsigned)list_edit_icon;
  if(list_edit_index<0&&list_picker)Wlists_toggle(&collections.lists[i],entries[selected].file);
  if(save_lists()){go_back();list_page=i/(list_picker?4:3);snprintf(status,sizeof(status),"Playlist opgeslagen.");}break;}
 case 39:change_screen(3);rebuild();break;
 case 40:list_page+=value;if(list_page<0)list_page=0;if(list_page>(int)(collections.count-1)/3)list_page=(collections.count-1)/3;break;
 case 35:lists_backup=collections;if(collections.count>1){memmove(&collections.lists[active_list],&collections.lists[active_list+1],(collections.count-active_list-1)*sizeof(WgbList));collections.count--;}else{collections.lists[0].count=0;strcpy(collections.lists[0].name,"Mijn playlist");}if(save_lists()){active_list=0;list_page=0;go_back();}break;
 case 17:change_screen(7);break;
 case 20:if(value<profile_count){active_profile=value;save_profiles();history_count=0;set_screen(0);rebuild();}break;
 case 21:if(value<profile_count)edit_profile(value);break;
 case 22:if(profile_count<4)edit_profile(-1);break;
 case 23:editing.avatar=(uint8_t)((editing.avatar+value+12)%12);break;
 case 24:choose_profile_name();break;
 case 25:{
  if(!editing.name[0]){choose_profile_name();if(!editing.name[0])break;}
  WgbProfile old[4];memcpy(old,profiles,sizeof(old));int old_count=profile_count;uint32_t old_id=next_profile_id;
  if(edit_index<0&&profile_count<4){profiles[profile_count++]=editing;next_profile_id++;}else if(edit_index>=0)profiles[edit_index]=editing;
  if(save_profiles()){status[0]=0;go_back();}else{memcpy(profiles,old,sizeof(old));profile_count=old_count;next_profile_id=old_id;snprintf(status,sizeof(status),"Profiel opslaan mislukt (SD-fout %d).",storage_errno);}break;
 }
 case 26:change_screen(9);break;
 case 27:if(edit_index>=0&&profile_count>1){for(int i=edit_index;i+1<profile_count;i++)profiles[i]=profiles[i+1];profile_count--;if(active_profile>edit_index)active_profile--;else if(active_profile==edit_index)active_profile=0;save_profiles();go_back();go_back();}break;
 }
}
static void handle_touch(unsigned int packet){
 if(!(packet&(1u<<18)))return;int x=(int)(packet&511),y=(int)((packet>>9)&255);int gesture=(int)(packet>>19);
 if(gesture&& !modal &&(screen==1||screen==3||screen==4||screen==6)){
  if(gesture==3||gesture==4){dispatch(9,gesture==3?1:-1);return;}
  int row=0,target=(y-36)/91+row_page;
  for(int c=0;c<5;c++)if(group_sizes[c]){if(row++==target){offsets[c]+=(gesture==1?1:-1);rebuild();break;}}return;
 }
 if(gesture&&screen==0){dispatch(7,gesture==1?1:-1);return;}
 if(gesture)return;
 for(int i=hit_count-1;i>=0;i--){Hit* h=&hits[i];if(x>=h->x&&x<h->x+h->w&&y>=h->y&&y<h->y+h->h){pending_action=h->action;pending_value=h->value;pressed_at=osGetTime();break;}}
}
void Wgb_main(void){
 Wmusic_tick();
 if(first){first=false;loading_time=scene_time=osGetTime();rebuild();start_job(1);}
 if(worker&&__atomic_load_n(&done,__ATOMIC_ACQUIRE)){
  threadJoin(worker,UINT64_MAX);threadFree(worker);worker=NULL;
  if(!result){if(job==1){count=incoming_count;memcpy(entries,incoming,sizeof(Entry)*(size_t)count);selected=0;rebuild();scene_time=osGetTime();snprintf(status,sizeof(status),"%d video's geladen",count);}else snprintf(status,sizeof(status),"Download klaar. Tik op afspelen.");}
  else snprintf(status,sizeof(status),"%s",job==1?"Geen verbinding. De SD-bibliotheek blijft beschikbaar.":"Download mislukt of geannuleerd.");
 }
 unsigned int keys=__atomic_exchange_n(&pending_keys,0,__ATOMIC_ACQUIRE),touch=__atomic_exchange_n(&pending_touch,0,__ATOMIC_ACQUIRE);
 if(worker&&job==2){if((keys&2)||((touch&(1u<<18))&&(touch&511)<40&&((touch>>9)&255)<36))__atomic_store_n(&cancel_job,1,__ATOMIC_RELAXED);}
 else{
  if(worker&&(keys&2))__atomic_store_n(&cancel_job,1,__ATOMIC_RELAXED);
  if(pending_action>=0&&osGetTime()-pressed_at>=250){int a=pending_action,v=pending_value;pending_action=-1;dispatch(a,v);}
  if(pending_action<0)handle_touch(touch);
  if(keys&2)dispatch(1,0);if(keys&16)dispatch(14,0);if(keys&64)dispatch(16,0);
  if(keys&128){if(screen==0)dispatch(7,-1);else dispatch(9,-1);}
  if(keys&256){if(screen==0)dispatch(7,1);else dispatch(9,1);}
  if(keys&4)dispatch(9,-1);if(keys&8)dispatch(9,1);
  if(keys&1){if(modal)dispatch(11,0);else if(screen==0&&group_count)dispatch(6,home_index);}
  if(Vid_query_running_flag())return;
 }
 hit_count=0;
 Draw_frame_ready();Draw_screen_ready(DRAW_SCREEN_TOP_LEFT,0xff000000);
 if(worker&&job==2)Wskin_loading(elapsed(loading_time),"Even geduld...",400,240);
 else{Wskin_image(WGB_BACKGROUND,0,-30,400,300);text_center("WANAGOBACK",0,93,400,24,0xffffffff);text_center("Kies je video op het touchscreen",0,137,400,11,0xff303030);}
 Draw_screen_ready(DRAW_SCREEN_BOTTOM,0xffe8bc31);Wskin_image(WGB_BACKGROUND,0,0,320,240);
 if((worker&&!count&&screen<7)||(worker&&job==2)){
  Wskin_loading(elapsed(loading_time),"Even geduld...",320,240);Wskin_image(WGB_BACK,3,3,27,27);
  if(job==2){char bytes[70];snprintf(bytes,sizeof(bytes),"%u KiB ontvangen - B: annuleren",__atomic_load_n(&downloaded,__ATOMIC_RELAXED));text_center(bytes,0,216,320,7,0xff222222);}
 }else{
  if(screen==7)draw_profiles();else if(screen==8)draw_profile_editor();else if(screen==9){icon(WGB_BACK,3,3,26,1,0);Wskin_image(WGB_PANEL,15,65,290,120);text_center("PROFIEL VERWIJDEREN?",30,88,260,14,0xff222222);text_button("ANNULEREN",40,139,108,1,0);text_button("VERWIJDEREN",172,139,108,27,0);}else if(screen==11){icon(WGB_BACK,3,3,26,1,0);Wskin_image(WGB_PANEL,15,65,290,120);text_center("PLAYLIST VERWIJDEREN?",25,87,270,13,0xff222222);text_button("ANNULEREN",35,139,115,1,0);text_button("VERWIJDEREN",170,139,115,35,0);}else{if(screen!=13)navbar();if(screen==13)draw_list_editor();else if(screen==12)draw_collection_home();else if(screen==10)draw_lists();else if(screen==0)draw_home();else if(screen==5)settings();else draw_browse();}
  if(modal){hit_count=0;draw_modal();}
  if(!count)Wskin_text(status,15,219,7,0xff222222,290,2);
 }
 Draw_apply_draw();
}
void Wgb_video_draw(double position,double duration,bool paused,bool loading){
 Draw_screen_ready(DRAW_SCREEN_BOTTOM,0xff000000);
 Wskin_image(WGB_BACK,3,3,24,24);
 Wskin_text(count?entries[selected].title:"Video",40,7,10,0xffffffff,271,2);
 Wskin_image(WGB_PLAY,139,116,42,42);
 if(!paused){Wskin_rect(151,126,5,23,0xfff8f8f8);Wskin_rect(164,126,5,23,0xfff8f8f8);}
 Wskin_image(WGB_LEFT,87,125,23,23);Wskin_image(WGB_RIGHT,211,125,23,23);
 char t[64];unsigned int p=(unsigned int)fmax(0,position/1000),d=(unsigned int)fmax(0,duration/1000);
 snprintf(t,sizeof(t),"%u:%02u / %u:%02u",p/60,p%60,d/60,d%60);text_center(t,25,170,270,9,0xffffffff);
 Wskin_round(15,209,290,8,4,0xff555555);float fraction=duration>0?(float)fmin(1,position/duration):0;
 if(fraction>0)Wskin_round(15,209,fmaxf(8,290*fraction),8,4,0xff00cdff);
 text_center(Wmega_error()[0]?Wmega_error():(loading||Wmega_buffering())?"Even geduld...":"A: pauze   B: terug   tik op de balk om te spoelen",0,228,320,6.5f,0xffffffff);
}
int Wgb_video_input(const Hid_info* key,double* value){
 if(DEF_HID_PHY_PR(key->b))return 2;if(DEF_HID_PHY_PR(key->a))return 1;
 if(DEF_HID_PHY_PR(key->d_left)){*value=-10000;return 3;}if(DEF_HID_PHY_PR(key->d_right)){*value=10000;return 3;}
 if(DEF_HID_PHY_RE(key->touch)){
  int x=key->touch_x_last,y=key->touch_y_last;
  if(y<34&&x<34)return 2;
  if(y>=112&&y<=163){if(x>=132&&x<=190)return 1;if(x>=72&&x<=123){*value=-10000;return 3;}if(x>=199&&x<=246){*value=10000;return 3;}}
  if(y>=196&&y<=224){*value=fmin(1,fmax(0,(x-15)/290.0));return 4;}
 }return 0;
}
