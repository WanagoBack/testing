/* Replace files on SD drivers whose rename cannot overwrite an existing target. */
#include "wgb_storage.h"
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
bool Wstore_replace(const char* temporary,const char* target){
 if(!temporary||!target)return false;
 if(rename(temporary,target)==0)return true;
 struct stat info;if(stat(target,&info)!=0)return false;
 char backup[512];if(snprintf(backup,sizeof(backup),"%s.bak",target)>=(int)sizeof(backup))return false;
 if(remove(backup)!=0&&errno!=ENOENT)return false;
 if(rename(target,backup)!=0)return false;
 if(rename(temporary,target)==0)return true;
 int cause=errno;rename(backup,target);errno=cause;return false;
}
