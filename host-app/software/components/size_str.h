#ifndef SIZE_STR_H
#define SIZE_STR_H

#include "integer.h"

void size_to_string_bytes(uint64_t size, char *buffer); // 4 characters and a unit: bytes, K, M, G
void size_to_string_sectors(uint32_t size, char *buffer);

#endif
