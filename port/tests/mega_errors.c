#include "wgb_mega.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char**argv){assert(argc==3);char path[96];assert(Wmega_path(argv[1],path,sizeof(path)));Wmega_begin();Wmega_stream*s=Wmega_open(path);unsigned char b[32];if(s){assert(Wmega_read(s,b,sizeof(b))<0);Wmega_close(s);}assert(strstr(Wmega_error(),argv[2]));puts("PASS: expected error reported");return 0;}
