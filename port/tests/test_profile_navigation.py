"""Exercise production profile persistence and navigation functions on the host."""
import pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[1]
source=(root/'source/wanagoback.c').read_text()
def function(name):
 start=source.index('static ',source.index('static '+('bool ' if name=='save_profiles' else 'void ')+name))
 brace=source.index('{',start);depth=1;i=brace+1
 while depth:
  if source[i]=='{':depth+=1
  if source[i]=='}':depth-=1
  i+=1
 return source[start:i]
with tempfile.TemporaryDirectory() as d:
 pre='''#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <assert.h>
#define ROOT "'''+d+'''/"
typedef struct {char name[49];uint32_t id;uint8_t avatar;} WgbProfile;
static WgbProfile profiles[4],editing;
static int profile_count,active_profile;
static uint32_t next_profile_id=2;
static int screen=7,home_index,row_page,pending_action=-1,hit_count,return_screen;
static int offsets[5];static float visual_offsets[5];static bool modal;
static unsigned pending_touch;static uint64_t scene_time;
typedef struct {int screen,home,row,offset[5];} Nav;
static Nav history[16];static int history_count;
static uint64_t osGetTime(void){return 1;}
static void rebuild(void){}
'''
 code=pre+'\n'.join(function(n) for n in ['save_profiles','load_profiles','set_screen','change_screen','go_back'])+'''
int main(void){
 load_profiles();assert(profile_count==1&&profiles[0].avatar==8);
 profile_count=2;strcpy(profiles[1].name,"Axel");profiles[1].id=2;profiles[1].avatar=11;next_profile_id=3;active_profile=1;assert(save_profiles());
 memset(profiles,0,sizeof(profiles));profile_count=0;load_profiles();assert(profile_count==2&&active_profile==1&&profiles[1].avatar==11&&!strcmp(profiles[1].name,"Axel"));
 set_screen(0);home_index=8;change_screen(1);row_page=2;offsets[1]=3;change_screen(5);change_screen(4);
 go_back();assert(screen==5);go_back();assert(screen==1&&row_page==2&&offsets[1]==3&&home_index==8);
 modal=true;return_screen=1;go_back();assert(!modal&&screen==1);go_back();assert(screen==0&&home_index==8);go_back();assert(screen==7);
 change_screen(8);change_screen(9);go_back();assert(screen==8);go_back();assert(screen==7);
 FILE* f=fopen(ROOT "profiles.dat","wb");fputs("corrupt",f);fclose(f);load_profiles();assert(profile_count==1&&active_profile==0&&!strcmp(profiles[0].name,"Samson"));
 puts("PASS: profile roundtrip, active avatar, corruption recovery, modal close, nested back history, category position, edit cancellation.");
}
'''
 path=pathlib.Path(d)/'test.c';path.write_text(code)
 subprocess.run(['gcc','-std=c99',str(path),'-o',d+'/test'],check=True)
 subprocess.run([d+'/test'],check=True)
