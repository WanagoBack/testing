#pragma once
typedef struct {char title[192],folder[80],source[1536],url[1536],file[32],description[384],category[40],meta[96],stream[1536];} Entry;
int Wcatalog_parse(const char* json,Entry* out,int capacity);
