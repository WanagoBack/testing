#include "wgb_lists.h"
#include "wgb_storage.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#define MAGIC 0x574c5332u
static bool valid_list(const WgbList* l){
 if(l->icon<1||l->icon>15||l->count>WLIST_ITEMS||!memchr(l->name,0,sizeof(l->name)))return false;
 for(unsigned i=0;i<l->count;i++)if(!l->keys[i][0]||!memchr(l->keys[i],0,32))return false;
 return true;
}
bool Wlists_load(const char* path,WgbLists* lists){
 char backup[512];snprintf(backup,sizeof(backup),"%s.bak",path);
 const char* paths[]={path,backup};
 for(int j=0;j<2;j++){
  FILE* f=fopen(paths[j],"rb");if(!f)continue;
  bool ok=fread(lists,sizeof(*lists),1,f)==1;fclose(f);
  ok=ok&&lists->magic==MAGIC&&lists->count>0&&lists->count<=WLIST_MAX&&valid_list(&lists->favorites);
  if(ok)for(unsigned i=0;i<lists->count;i++)if(!valid_list(&lists->lists[i])||!lists->lists[i].name[0])ok=false;
  if(ok)return true;
 }
 memset(lists,0,sizeof(*lists));lists->magic=MAGIC;lists->count=1;
 lists->favorites.icon=lists->lists[0].icon=1;
 strcpy(lists->favorites.name,"Favorieten");strcpy(lists->lists[0].name,"Mijn playlist");return false;
}
bool Wlists_save(const char* path,const WgbLists* lists){
 char temp[512];if(snprintf(temp,sizeof(temp),"%s.part",path)>=(int)sizeof(temp)){errno=ENAMETOOLONG;return false;}
 FILE* f=fopen(temp,"wb");if(!f)return false;
 bool ok=fwrite(lists,sizeof(*lists),1,f)==1;if(fclose(f))ok=false;
 if(ok)ok=Wstore_replace(temp,path);if(!ok){int cause=errno;remove(temp);errno=cause;}return ok;
}
bool Wlists_has(const WgbList* list,const char* key){for(unsigned i=0;i<list->count;i++)if(!strcmp(list->keys[i],key))return true;return false;}
bool Wlists_toggle(WgbList* list,const char* key){
 for(unsigned i=0;i<list->count;i++)if(!strcmp(list->keys[i],key)){
  memmove(list->keys+i,list->keys+i+1,(list->count-i-1)*32);list->count--;return true;
 }
 if(list->count==WLIST_ITEMS||strlen(key)>=32){errno=ENOSPC;return false;}
 strcpy(list->keys[list->count++],key);return true;
}
