#include "fastseek.h"
#include <stdio.h>
#include <stdlib.h>

#if FF_USE_FASTSEEK

// How many link maps exist now and the most there were (T128 S9: the folder disk keeps at most FH_HANDLES files open, so at most that many maps).
static uint32_t g_live, g_peak, g_lines;
uint32_t fastseek_maps_live(void) { return g_live; }
uint32_t fastseek_maps_peak(void) { return g_peak; }

DWORD *fastseek_build(FIL *fp, uint32_t *needed)
{
    uint32_t items = FASTSEEK_START_ITEMS;
    *needed = 0;
    for (int attempt = 0; attempt < 2; attempt++) {
        DWORD *tbl = (DWORD *)malloc(items * sizeof(DWORD));
        if (!tbl) {
            *needed = 1;
            return 0;
        }
        tbl[0] = items;
        fp->cltbl = tbl;
        FRESULT r = f_lseek(fp, CREATE_LINKMAP);
        if (r == FR_OK) {
            if (++g_live > g_peak) g_peak = g_live;
            return tbl;
        }
        fp->cltbl = 0;                 // FatFs leaves the pointer set on failure, with an unterminated table behind it
        uint32_t want = tbl[0];        // on FR_NOT_ENOUGH_CORE: the items the file needs
        free(tbl);
        if (r != FR_NOT_ENOUGH_CORE) {
            *needed = 2;
            return 0;
        }
        if (attempt || want > FASTSEEK_MAX_ITEMS) {
            *needed = want;
            return 0;
        }
        items = want;
    }
    return 0;
}

void fastseek_drop(FIL *fp, DWORD *tbl)
{
    fp->cltbl = 0;
    if (tbl) g_live--;
    free(tbl);
}

void fastseek_free(DWORD *tbl)
{
    if (tbl) g_live--;
    free(tbl);
}

void FastSeek::access(FIL *fp, uint64_t off, uint32_t len, bool write, const char *name)
{
    uint64_t size = fp->obj.objsize;
    bool grows = write && off + len > size;
    if (grows && map_) {
        fastseek_drop(fp, map_);
        map_ = 0;
        tried_ = true;
        if (g_lines < 40) { g_lines++; printf("[core] fast seek: %s grows, link map dropped\n", name); }
    }
    if (!tried_ && !grows && off != next_ && size >= FASTSEEK_MIN_SIZE) {
        tried_ = true;
        uint32_t need = 0;
        map_ = fastseek_build(fp, &need);
        if (g_lines >= 40) {                       // a folder disk opens many files, one line each would flood the console
        } else if (map_) {
            g_lines++;
            printf("[core] fast seek: %s, link map of %u fragment(s)\n", name, (unsigned)((map_[0] - 2) / 2));
        } else {
            g_lines++;
            printf("[core] fast seek: %s, no link map (%s), normal seek\n", name,
                   need == 1 ? "no memory" : need == 2 ? "FatFs error" : "too many fragments");
        }
    }
    next_ = off + len;
}

#endif
