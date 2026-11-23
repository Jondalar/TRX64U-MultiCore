// The table of the library "fastseek" (T239.3 P3-1): the cluster link map of an open FatFs file (fastseek.h). The file manager's CoreFs (fm_fs.cc) calls it before each read or write of an
// open file and no function of the library; an app linked without the library has no table and seeks the normal way. The library gives the table to the CoreFs in its `bind` entry
// (fastseek_exports.cc: fm_fs_set_fastseek). Only when FatFs is built with FF_USE_FASTSEEK. Size first; within a major members are only appended.
#ifndef COREAPP_FASTSEEK_API_H
#define COREAPP_FASTSEEK_API_H

#include "ff.h"
#include <stdint.h>

#if FF_USE_FASTSEEK

typedef struct FastSeekApi {
    uint32_t size;
    void *(*make)(void);                                                                              // the policy state of one open file (null: no memory)
    // Before the read or write [off, off+len) on the open file fp: builds the map at the first seek and drops it when a write extends the file. name only for the log.
    void (*access)(void *state, FIL *fp, uint64_t off, uint32_t len, bool write, const char *name);
    void (*destroy)(void *state);                                                                     // frees the map and the state
    uint32_t (*maps_live)(void);                                                                      // link maps that exist now
    uint32_t (*maps_peak)(void);                                                                      // the most that existed at one time
} FastSeekApi;

// The table itself (the host application and the tests give it to fm_fs_set_fastseek directly; an app of a TOML B gets it by the bind entry).
const FastSeekApi *fastseek_table(void);

#endif

#endif
