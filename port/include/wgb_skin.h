#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "wgb_assets.h"
void Wskin_init(void);
void Wskin_exit(void);
void Wskin_image(int id,float x,float y,float w,float h);
void Wskin_rect(float x,float y,float w,float h,uint32_t color);
void Wskin_round(float x,float y,float w,float h,float r,uint32_t color);
void Wskin_text(const char* text,float x,float y,float size,uint32_t color,float max_width,int max_lines);
float Wskin_width(const char* text,float size);
void Wskin_thumb(const char* file,float x,float y,float w,float h);
void Wskin_loading(float seconds,const char* text,float w,float h);

void Wskin_video_mode(bool active);
