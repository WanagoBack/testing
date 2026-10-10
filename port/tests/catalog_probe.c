#include "wgb_catalog.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char**argv){char s[100000];FILE*f=fopen(argc>1?argv[1]:"romfs/wgb/library.json","rb");assert(f);size_t n=fread(s,1,sizeof(s)-1,f);s[n]=0;fclose(f);Entry*e=calloc(512,sizeof(*e));assert(Wcatalog_parse(s,e,512)==21);assert(!strcmp(e[0].title,"Klus op dieet"));assert(!strcmp(e[0].folder,"Plop"));assert(Wcatalog_parse("{\"videos\":[{\"title\":\"caf\\u00e9\",\"megaUrl3ds\":\"test\"},{\"title\":\"\"}]}",e,512)==1);assert(!strcmp(e[0].title,"caf\xc3\xa9")&&!strcmp(e[0].stream,"test"));assert(Wcatalog_parse("broken",e,512)<0);free(e);puts("PASS: 21 real videos, Unicode, empty-title filtering, low-quality URL, malformed input.");}
