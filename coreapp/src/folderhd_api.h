// The table of the library "folderhd" (T239.3 P3-1): a folder of a medium as a hard disk (folderhd.h). The sector engine (sectors.cc) mounts a folder through it and calls no function
// of the library; an app linked without the library has no table, and mounting a folder is refused with a reason. The library gives the table to the engine in its `bind` entry
// (folderhd_exports.cc: sec_set_folderhd). Size first; within a major members are only appended.
#ifndef COREAPP_FOLDERHD_API_H
#define COREAPP_FOLDERHD_API_H

#include <stdint.h>
#include "fsif.h"
#include "io.h"

// What the engine tells about a mounted folder disk.
typedef struct FolderHdInfo {
    bool host_writable;                       // false: the medium is read only, the Amiga's writes are lost at eject
    uint32_t cylinders, nodes;                // the layout and the objects of the folder tree
    const char *report;                       // what was left out and why, "" when nothing (valid as long as the disk is mounted)
} FolderHdInfo;

typedef struct FolderHdApi {
    uint32_t size;
    // The most cylinders (cylinder 0 included) a disk of whole cylinders can have in `max_bytes`.
    uint32_t (*max_cylinders)(uint64_t max_bytes);
    // The most the menu can offer for the folder (free space less the reserve, plus the tree that is on it), at most `max_cylinders` cylinders; 0: nothing. `at_least`: a size the caller wants backed.
    uint64_t (*offer_bytes)(CoreFs *fs, const char *folder, uint32_t max_cylinders, uint64_t at_least);
    // "2", "0.5", "1.25" (GB, two decimals); empty is `def`. false: not a number.
    bool (*parse_gb)(const char *text, uint64_t def, uint64_t *out);
    // The folder as a disk (a CoreFile the engine serves sectors from), or null with the reason in msg.
    CoreFile *(*mount)(CoreFs *fs, CoreIo *io, int unit, const char *folder, uint64_t want_bytes, uint32_t max_cylinders, char *msg, int msglen);
    bool (*dirty)(CoreFile *disk);            // the Amiga's writes are not all in the host files yet
    bool (*idle_slice)(CoreFile *disk);       // one slice of the fold-back; true while there is more
    void (*unmount)(CoreFile *disk);          // frees the disk (not closed through CoreFs)
    void (*info)(CoreFile *disk, FolderHdInfo *out);
} FolderHdApi;

// The table itself (the tests give it to sec_set_folderhd directly; an app of a TOML B gets it by the bind entry).
const FolderHdApi *folderhd_table(void);

#endif
