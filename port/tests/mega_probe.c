/* Host integration probe: build with the production wgb_mega.c and libavutil. */
#include "wgb_mega.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc,char** argv){
 assert(argc==3);char path[96];
 assert(!Wmega_path("https://example.com/file/abcdefgh#bad",path,sizeof(path)));
 assert(!Wmega_path("https://mega.nz/folder/abcdefgh#bad",path,sizeof(path)));
 assert(Wmega_path(argv[1],path,sizeof(path)));Wmega_begin();
 Wmega_stream* s=Wmega_open(path);if(!s){fprintf(stderr,"%s\n",Wmega_error());return 1;}
 long long size=Wmega_seek(s,0,0x10000);assert(size>1048576);
 assert(Wmega_seek(s,-1,SEEK_SET)<0);assert(Wmega_seek(s,1,SEEK_END)<0);
 unsigned char* data=malloc(1024*1024);assert(data);
 FILE* f=fopen(argv[2],"wb");assert(f);
 const long long pos[]={0,524281,1234567,size-524288};
 for(unsigned i=0;i<sizeof(pos)/sizeof(pos[0]);i++){
  assert(Wmega_seek(s,pos[i],SEEK_SET)==pos[i]);int got=0,limit=i==0?1048576:(i==3?524288:131072);
  while(got<limit){int n=Wmega_read(s,data+got,limit-got);if(n<=0){fprintf(stderr,"%s\n",Wmega_error());return 2;}got+=n;}
  fwrite(&pos[i],8,1,f);fwrite(&limit,4,1,f);fwrite(data,1,limit,f);
 }
 assert(Wmega_seek(s,0,SEEK_END)==size);assert(Wmega_read(s,data,16)==0);
 Wmega_cancel();assert(Wmega_read(s,data,16)<0);Wmega_close(s);fclose(f);free(data);
 printf("PASS: metadata, decrypt reads, cross-block reads, seek, EOF, cancel. Size=%lld\n",size);
}
