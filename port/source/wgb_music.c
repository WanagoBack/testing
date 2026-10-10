/* Bounded menu music queue. PCM WAV, supplied by the user, never mixed over videos. */
#include "wgb_music.h"
#include <3ds.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define MUSIC_CH 23
#define MUSIC_BYTES 16384
static FILE* song;
static ndspWaveBuf queue[3];
static uint8_t* data[3];
static bool enabled=true,initialized,playing,tried;
static long audio_start;
static uint32_t audio_size,remaining;
static unsigned channels,rate;
static const char* message="Muziek: bestand ontbreekt";
static uint32_t le32(const unsigned char*p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static bool open_song(void){
 song=fopen("sdmc:/3ds/WanagoBack/music/menu.wav","rb");if(!song)song=fopen("romfs:/wgb/menu.wav","rb");if(!song)return false;
 unsigned char b[16];if(fread(b,1,12,song)!=12||memcmp(b,"RIFF",4)||memcmp(b+8,"WAVE",4))goto fail;
 bool format=false;
 for(int i=0;i<64;i++){
  if(fread(b,1,8,song)!=8)goto fail;uint32_t n=le32(b+4);long next=ftell(song)+(long)n+(long)(n&1);
  if(!memcmp(b,"fmt ",4)){if(n<16||fread(b,1,16,song)!=16)goto fail;channels=b[2]|(unsigned)b[3]<<8;rate=le32(b+4);if(b[0]!=1||b[1]||b[14]!=16||b[15]||(channels!=1&&channels!=2)||rate<8000||rate>48000)goto fail;format=true;}
  else if(!memcmp(b,"data",4)){if(!format||!n||n>128*1024*1024)goto fail;audio_start=ftell(song);audio_size=n-n%(channels*2);remaining=audio_size;return audio_size>0;}
  if(n>128*1024*1024||fseek(song,next,SEEK_SET))goto fail;
 }
fail:fclose(song);song=NULL;message="Muziek: gebruik PCM WAV (16-bit)";return false;
}
void Wmusic_stop(void){if(initialized)ndspChnWaveBufClear(MUSIC_CH);if(song)fclose(song);song=NULL;playing=false;tried=false;}
void Wmusic_tick(void){
 if(!enabled)return;
 if(!playing){
  if(tried)return;tried=true;if(!open_song())return;
  if(!initialized){if(ndspInit()!=0){fclose(song);song=NULL;message="Muziek: DSP-audio niet beschikbaar";return;}initialized=true;for(int i=0;i<3;i++)data[i]=linearAlloc(MUSIC_BYTES);}
  for(int i=0;i<3;i++)if(!data[i]){fclose(song);song=NULL;message="Muziek: onvoldoende geheugen";return;}
  ndspChnReset(MUSIC_CH);ndspChnSetInterp(MUSIC_CH,NDSP_INTERP_LINEAR);ndspChnSetRate(MUSIC_CH,(float)rate);ndspChnSetFormat(MUSIC_CH,channels==1?NDSP_FORMAT_MONO_PCM16:NDSP_FORMAT_STEREO_PCM16);
  float mix[12]={.35f,.35f,0};ndspChnSetMix(MUSIC_CH,mix);memset(queue,0,sizeof(queue));playing=true;message="Menumuziek: aan";
 }
 for(int i=0;i<3;i++)if(queue[i].status==NDSP_WBUF_FREE||queue[i].status==NDSP_WBUF_DONE){
  size_t used=0;while(used<MUSIC_BYTES){if(!remaining){if(fseek(song,audio_start,SEEK_SET))break;remaining=audio_size;}size_t n=MUSIC_BYTES-used;if(n>remaining)n=remaining;size_t got=fread(data[i]+used,1,n,song);if(!got)break;used+=got;remaining-=(uint32_t)got;}
  if(!used){Wmusic_stop();message="Muziek: leesfout";tried=true;return;}
  memset(&queue[i],0,sizeof(queue[i]));queue[i].data_vaddr=data[i];queue[i].nsamples=(uint32_t)used/(channels*2);DSP_FlushDataCache(data[i],used);ndspChnWaveBufAdd(MUSIC_CH,&queue[i]);
 }
}
void Wmusic_toggle(void){enabled=!enabled;Wmusic_stop();message=enabled?"Muziek: bestand ontbreekt":"Menumuziek: uit";}
const char* Wmusic_status(void){return message;}
void Wmusic_exit(void){Wmusic_stop();if(initialized){ndspChnReset(MUSIC_CH);for(int i=0;i<3;i++){linearFree(data[i]);data[i]=NULL;}ndspExit();initialized=false;}}
