#pragma once
#include <stdbool.h>
#include <stdint.h>
#define WLIST_MAX 8
#define WLIST_ITEMS 512
typedef struct {char name[49];uint32_t count,icon;char keys[WLIST_ITEMS][32];} WgbList;
typedef struct {uint32_t magic,count;WgbList favorites,lists[WLIST_MAX];} WgbLists;
bool Wlists_load(const char* path,WgbLists* lists);
bool Wlists_save(const char* path,const WgbLists* lists);
bool Wlists_has(const WgbList* list,const char* key);
bool Wlists_toggle(WgbList* list,const char* key);
