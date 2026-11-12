// Fast seek on a FatFs file (T104 5.16): the cluster link map (CLMT) of FF_USE_FASTSEEK. Without it f_lseek to an earlier cluster walks the
// FAT chain from the start of the file; a block 84 MB into a 2 GiB image costs thousands of FAT lookups per seek. With it a seek is one
// table walk over the file's fragments, whatever the offset. FatFs cannot grow a file through the map (f_write beyond the table ends
// FR_DISK_FULL), so a writer that extends the file drops the map first (fastseek_drop).
#ifndef COREAPP_FASTSEEK_H
#define COREAPP_FASTSEEK_H

#include "ff.h"
#include <stdint.h>

#if FF_USE_FASTSEEK

enum {
    FASTSEEK_START_ITEMS = 128,        // first table: 1 + 2 * 62 fragments + terminator
    FASTSEEK_MAX_ITEMS = 16384,        // 64 KiB of table, 8191 fragments; a file more scattered than that stays on the slow path
    FASTSEEK_MIN_SIZE = 256 * 1024,    // smaller files are read in one or two goes; the walk to build the map costs more than it saves
};

// Builds the map of an open file: the table is allocated (malloc), the first try is FASTSEEK_START_ITEMS, a second one has exactly the size FatFs
// reported as needed. Returns the table (fp->cltbl points at it) or NULL with fp->cltbl cleared: the file is read the slow way as before.
// *needed is 0 on success, the item count FatFs asked for when it did not fit FASTSEEK_MAX_ITEMS, 1 when no memory, 2 on a FatFs error.
DWORD *fastseek_build(FIL *fp, uint32_t *needed);

// Drops the map: fp->cltbl cleared, the table freed. tbl may be NULL.
void fastseek_drop(FIL *fp, DWORD *tbl);

uint32_t fastseek_maps_live(void);       // link maps that exist now
uint32_t fastseek_maps_peak(void);       // the most that existed at one time

// Frees a table without touching the file object.
void fastseek_free(DWORD *tbl);

// The policy per open file: when the map is built and when it must go. One per open image, next to the file object.
//  - built at the first access that does not continue the previous one (a seek), and only for a file of FASTSEEK_MIN_SIZE and more: a file
//    that is read from top to bottom, or in one go, never pays for the walk of its chain;
//  - tried once; a refusal (too scattered, no memory) leaves the file on the normal seek, for good;
//  - a write that reaches beyond the end of the file drops the map before the write, because FatFs cannot extend a file through it.
class FastSeek {
    DWORD *map_;
    bool tried_;
    uint64_t next_;
public:
    FastSeek() : map_(0), tried_(false), next_(0) { }
    ~FastSeek() { fastseek_free(map_); }
    // Before the read or write [off, off+len) on the open file fp. name only for the log.
    void access(FIL *fp, uint64_t off, uint32_t len, bool write, const char *name);
    bool active() const { return map_ != 0; }
};

#endif

#endif
