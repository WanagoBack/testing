#include "wgb_skin.h"
#include "system/draw/draw.h"
#include "stb_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
static Draw_image_data images[WGB_ASSET_COUNT];
static bool loaded[WGB_ASSET_COUNT];
static Draw_image_data solid;
static struct {char path[160];Draw_image_data image;bool loaded;unsigned long used;} thumbs[8];
static unsigned long clock_tick;
static unsigned int pot(unsigned int n){unsigned int p=16;while(p<n)p*=2;return p;}
static bool load_texture(const char* path,Draw_image_data* texture){
 int w,h,c;unsigned char* rgba=stbi_load(path,&w,&h,&c,4);
 if(!rgba)return false;
 if(w<=0||h<=0||w>512||h>512){stbi_image_free(rgba);return false;}
 unsigned int tw=pot((unsigned int)w),th=pot((unsigned int)h);
 if(Draw_texture_init(texture,(uint16_t)tw,(uint16_t)th,RAW_PIXEL_ABGR8888)){stbi_image_free(rgba);return false;}
 unsigned char* dst=texture->c2d.tex->data;memset(dst,0,tw*th*4);
 for(unsigned int y=0;y<(unsigned)h;y++)for(unsigned int x=0;x<(unsigned)w;x++){
  unsigned int tile=((y>>3)*(tw>>3)+(x>>3))*64;
  unsigned int mx=(x&1)|((x&2)<<1)|((x&4)<<2),my=((y&1)<<1)|((y&2)<<2)|((y&4)<<3);
  unsigned int d=(tile+mx+my)*4,s=(y*(unsigned)w+x)*4;
  dst[d]=rgba[s+3];dst[d+1]=rgba[s+2];dst[d+2]=rgba[s+1];dst[d+3]=rgba[s];
 }
 texture->subtex->width=(uint16_t)w;texture->subtex->height=(uint16_t)h;
 texture->subtex->left=0;texture->subtex->top=1;texture->subtex->right=(float)w/(float)tw;texture->subtex->bottom=1-(float)h/(float)th;
 texture->c2d.subtex=texture->subtex;C3D_TexFlush(texture->c2d.tex);Draw_texture_set_filter(texture,true);stbi_image_free(rgba);return true;
}
void Wskin_init(void){
 solid=Draw_get_empty_image();
 for(int i=0;i<WGB_ASSET_COUNT;i++){char path[160];snprintf(path,sizeof(path),"romfs:/wgb/%s",wgb_asset_names[i]);loaded[i]=load_texture(path,&images[i]);}
}
void Wskin_exit(void){for(int i=0;i<WGB_ASSET_COUNT;i++)if(loaded[i])Draw_texture_free(&images[i]);for(int i=0;i<8;i++)if(thumbs[i].loaded)Draw_texture_free(&thumbs[i].image);}
void Wskin_image(int id,float x,float y,float w,float h){if(id>=0&&id<WGB_ASSET_COUNT&&loaded[id])Draw_texture(&images[id],0,x,y,w,h);}
void Wskin_rect(float x,float y,float w,float h,uint32_t color){Draw_texture(&solid,color,x,y,w,h);}
void Wskin_round(float x,float y,float w,float h,float r,uint32_t color){
 C2D_DrawRectSolid(x+r,y,0,w-2*r,h,color);C2D_DrawRectSolid(x,y+r,0,w,h-2*r,color);
 C2D_DrawCircleSolid(x+r,y+r,0,r,color);C2D_DrawCircleSolid(x+w-r,y+r,0,r,color);
 C2D_DrawCircleSolid(x+r,y+h-r,0,r,color);C2D_DrawCircleSolid(x+w-r,y+h-r,0,r,color);
}
static unsigned int next_char(const unsigned char** p){unsigned int c=*(*p)++;if(c>=192&&c<224&&**p){c=((c&31)<<6)|(*(*p)++&63);}else if(c>=224){while((**p&192)==128)(*p)++;c='?';}return c;}
float Wskin_width(const char* text,float size){float w=0;const unsigned char* p=(const unsigned char*)text;while(*p){unsigned int c=next_char(&p);if(c<32||c>255)c='?';w+=(wgb_font_widths[c-32]+.5f)*(size/25);}return w;}
void Wskin_text(const char* text,float x,float y,float size,uint32_t color,float max_width,int max_lines){
 if(!loaded[WGB_FONT]){Draw_c(text,x,y,size/20,color);return;}
 const unsigned char* p=(const unsigned char*)text;float pos=x,scale=size/25;int line=0;
 while(*p){unsigned int c=next_char(&p);if(c=='\n'){pos=x;y+=size*1.2f;if(++line>=max_lines)break;continue;}if(c<32||c>255)c='?';float advance=(wgb_font_widths[c-32]+.5f)*scale;
  if(pos+advance>x+max_width){pos=x;y+=size*1.2f;if(++line>=max_lines)break;}
  if(c!=32){unsigned int index=c-32;float sx=(index%16)*32,sy=(index/16)*32;
   Draw_texture_with_crop(&images[WGB_FONT],color,pos,y-5*scale,32*scale,32*scale,0,0,0,sx,sx+32,sy,sy+32);
  }pos+=advance;
 }
}
void Wskin_thumb(const char* file,float x,float y,float w,float h){
 char path[160];snprintf(path,sizeof(path),"/3ds/WanagoBack/thumbs/%s.png",file);
 int slot=-1;unsigned long oldest=~0UL;++clock_tick;
 for(int i=0;i<8;i++){if(!strcmp(thumbs[i].path,path)){slot=i;break;}}
 if(slot<0){for(int i=0;i<8;i++)if(thumbs[i].used<oldest){oldest=thumbs[i].used;slot=i;}
  if(thumbs[slot].loaded)Draw_texture_free(&thumbs[slot].image);memset(&thumbs[slot],0,sizeof(thumbs[slot]));snprintf(thumbs[slot].path,sizeof(thumbs[slot].path),"%s",path);thumbs[slot].loaded=load_texture(path,&thumbs[slot].image);
 }
 thumbs[slot].used=clock_tick;
 if(thumbs[slot].loaded)Draw_texture(&thumbs[slot].image,0,x,y,w,h);
 else Wskin_rect(x,y,w,h,0xffce8b14);
}
void Wskin_loading(float seconds,const char* text,float w,float h){
 Wskin_image(WGB_SUN,-w*.02f,-h*.02f,w*1.04f,h*1.04f);
 float bob=1.6f*(1-cosf(seconds*3.14159265f/3));
 float pw=w*.3125f,ph=pw*114.0f/140.0f;
 Wskin_image(WGB_LOADING_PLOP,w-pw-8,h*.96f-ph-bob,pw,ph);
 float cloud=3.2f*sinf(seconds*3.14159265f/4);
 Wskin_image(WGB_CLOUDS,-8+cloud,h*.79f,w+16,h*.30f);
 float pulse=.98f+.02f*(1-cosf(seconds*3.14159265f));float size=12*pulse;
 float tx=(w-Wskin_width(text,size))*.5f;
 Wskin_text(text,tx+1,h*.46f+1,size,0x80000000,w-20,2);Wskin_text(text,tx,h*.46f,size,0xffffffff,w-20,2);
}
