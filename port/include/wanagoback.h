#pragma once
#include <stdbool.h>
#include "system/util/hid_types.h"
void Wgb_init(void);
void Wgb_exit(void);
bool Wgb_ready(void);
void Wgb_main(void);
void Wgb_hid(const Hid_info* key);
bool Vid_wgb_open(const char* name, const char* directory);
