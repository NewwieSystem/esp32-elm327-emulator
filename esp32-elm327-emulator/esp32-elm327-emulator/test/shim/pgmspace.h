#pragma once
#include <string.h>
#define PROGMEM
#define PGM_P const char *
#define memcpy_P memcpy
#define strlen_P strlen
#define strcpy_P strcpy
#define strncpy_P strncpy
#define pgm_read_byte(a) (*(const unsigned char*)(a))
