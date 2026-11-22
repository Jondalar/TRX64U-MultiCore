// Base 3.3, a second listing call for `SvcFs` (T239.3 section 5; boundary 3.4): `SvcFsEntry` is frozen at 72 bytes with a 56 byte name (the app ABI), so a name of up to 255
// characters needs an entry of its own. The call is appended to SvcFs after `rename` (svctab.h is changed when 3.3 is built); SvcFsAdd is the same member as a struct of
// its own so that this header compiles alone. No other member of SvcFs changes; `CoreFs::open(rw)` needs no create flag (`save(path, "", 0)` first, then `open(path, true)`).
#ifndef COREAPP_SVC_FS_H
#define COREAPP_SVC_FS_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// The layout of FsEntry (fsif.h, FS_NAME_MAX 255): the base checks sizeof(SvcFsEntryLong) == sizeof(FsEntry) where both are in sight (libapi.h).
typedef struct SvcFsEntryLong {
    char name[256];                       // the whole name, zero terminated
    uint16_t fdate, ftime;                // FAT packed modification date and time, 0: unknown
    uint8_t attr;                         // FS_ATTR_*
    bool dir;
    uint64_t size;
} SvcFsEntryLong;

typedef struct SvcFsAdd {
    // Lists `dir` into out[0 .. max - 1], whole names. Result: the number of entries written (never more than max), -1 when `dir` is not a directory, the medium cannot
    // be read, or the base has no memory for the listing. max <= 0 writes nothing and gives the number of entries the directory has. `out` is the caller's (a library's
    // heap or stack); 264 bytes an entry, so a long listing is taken in a heap block and freed after. Task: any task that may block (the medium is read).
    int (*list_long)(const char *dir, SvcFsEntryLong *out, int max);
} SvcFsAdd;

#ifdef __cplusplus
}
#endif

#endif
