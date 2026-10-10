#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define WMEGA_PREFIX "wgbmega:"
typedef struct Wmega_stream Wmega_stream;
bool Wmega_path(const char* link,char* path,size_t capacity);
Wmega_stream* Wmega_open(const char* path);
int Wmega_read(Wmega_stream* stream,uint8_t* data,int count);
int64_t Wmega_seek(Wmega_stream* stream,int64_t offset,int whence);
void Wmega_close(Wmega_stream* stream);
void Wmega_begin(void);
void Wmega_cancel(void);
bool Wmega_buffering(void);
bool Wmega_cancelled(void);
const char* Wmega_error(void);
