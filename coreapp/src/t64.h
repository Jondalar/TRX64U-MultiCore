// The .t64 tape container (T104.2 UI, Library "Enter"): its directory as a list, one entry as the PRG image the C64 loads (load address first). Pure: CoreFs only.
#ifndef COREAPP_T64_H
#define COREAPP_T64_H

#include "fsif.h"

struct T64Entry {
    char name[20];                 // the entry's name, trailing blanks off
    uint16_t start, end;
    uint32_t offset;
};

// The used entries of the file that hold a program (at most max). Returns how many, -1 when it is not a T64 or cannot be read.
int t64_list(CoreFs *fs, const char *path, T64Entry *out, int max);
// Entry `index` of the list as a PRG image (two bytes of load address, then the data) into out. Returns its length, -1 on error.
int t64_load(CoreFs *fs, const char *path, int index, uint8_t *out, uint32_t max);

#endif
