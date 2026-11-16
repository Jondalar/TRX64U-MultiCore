// A folder listing that grows with the folder (GitHub #93, T209 "File Browser, n entries"): the Library and the copy/move folder pick hold their entries on the heap,
// START of them first, and read again with a buffer twice as large while the folder fills it, as long as the grown buffer costs no more than a quarter of the heap that
// is free (never more than HARD entries). What the budget leaves out is not cut in silence: `cut` says so and the caller shows a line for it.
//
// CoreFs::list has no "total" (every lister behind it - the FAT directory, a disk image, FTP, Online - would need one), so a full buffer is the sign of a longer folder.
#ifndef COREAPP_LISTGROW_H
#define COREAPP_LISTGROW_H

#include <stdint.h>
#include <stdlib.h>
#include "fsif.h"

#ifndef LISTGROW_START                                       // (a mock-up build for the pictures of the "more" line sets both small: -DLISTGROW_START=40 -DLISTGROW_HARD=60)
#define LISTGROW_START 512
#endif
#ifndef LISTGROW_HARD
#define LISTGROW_HARD 32768
#endif

struct ListGrow {
    enum { START = LISTGROW_START, HARD = LISTGROW_HARD };

    // The heap that is free now; the board wires xPortGetFreeHeapSize, a bench its own number. Null: unknown, only HARD bounds the list.
    static uint32_t (*&free_heap())(void)
    {
        static uint32_t (*f)(void) = 0;
        return f;
    }

    // `*buf` holds `*cap` entries (null: none yet). Reads `dir` into (*buf)[up ..]; returns what CoreFs::list returns (the entries read, -1 when the folder cannot be read).
    // `*cut` is set when the folder is longer than the buffer the budget allows.
    static int read(CoreFs *fs, const char *dir, FsEntry **buf, int *cap, int up, bool *cut)
    {
        *cut = false;
        if (!*buf) {
            *buf = (FsEntry *)malloc(sizeof(FsEntry) * START);
            *cap = *buf ? START : 0;
        }
        if (!*buf) {
            return -1;
        }
        for (;;) {
            int room = *cap - up;
            int n = fs->list(dir, *buf + up, room);
            if (n < 0 || n < room) {
                return n;
            }
            uint64_t want = (uint64_t)*cap * 2;
            uint32_t fh = free_heap() ? free_heap()() : 0xFFFFFFFFu;
            if (want > HARD || (want - (uint64_t)*cap) * sizeof(FsEntry) > fh / 4) {
                *cut = true;
                return n;
            }
            FsEntry *nb = (FsEntry *)malloc(sizeof(FsEntry) * (size_t)want);
            if (!nb) {
                *cut = true;
                return n;
            }
            free(*buf);                                    // the old entries are read again into the larger one
            *buf = nb;
            *cap = (int)want;
        }
    }
};

#endif
