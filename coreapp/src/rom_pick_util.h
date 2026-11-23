// Helpers of the shared drive-ROM pick (coreapp/cores/drive_rom_pick.h), kept here so the pick itself stays small (T192 D24).
#ifndef COREAPP_ROM_PICK_UTIL_H
#define COREAPP_ROM_PICK_UTIL_H

#include "ctx.h"
#include "sha256.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// The hex SHA-256 of a ROM, for the console line of what a drive holds.
static inline void rom_sha_hex(const uint8_t *d, uint32_t n, char *out)
{
    sha256_t h;
    uint8_t dig[32];
    sha256_init(&h);
    sha256_update(&h, d, n);
    sha256_final(&h, dig);
    sha256_hex(dig, out);
}

// T258 D6: the 8 KiB JiffyDOS file of the old 1541 is the upper half of the ROM (the chip 901229); the lower half (the chip 325302-01) is the first 8 KiB of
// the stock 1541 ROM. The drive maps a 32 KiB ROM as the CPU's window $8000-$FFFF, so the 1541 part is the last 16 KiB of the stock file (the whole file when it
// has 16 KiB). Builds the whole 16 KiB ROM into `out` (it may not overlap `half`) and returns its size, or 0 when there is no 16 KiB of stock ROM to take it from.
#define ROM_HALF 8192u
static inline uint32_t rom_half_over(uint8_t *out, const uint8_t *stock, uint32_t n_stock, const uint8_t *half)
{
    if (n_stock < 2 * ROM_HALF) {
        return 0;
    }
    memcpy(out, stock + (n_stock - 2 * ROM_HALF), ROM_HALF);
    memcpy(out + ROM_HALF, half, ROM_HALF);
    return 2 * ROM_HALF;
}

// T258 D10: the note of the row that selects the JiffyDOS ROM of a drive (the setting `key`, a file the person picks): when the picked file is the 8 KiB half and the stock ROM
// (n_stock, held by the pick) is missing, the half needs it, and where it goes. "" when nothing is missing. True from the moment the file is picked, no reset needed: the file's
// size comes from one stat, kept for the path it was made for (a menu draw never asks the file system twice for the same path); a failed stat is not kept (a stick mounted
// after the first draw is read at the next one), and `fresh` (the pick) reads the size again (a file replaced under the same name). One static text: the menu reads it at once.
static inline const char *rom_half_note(CoreCtx *c, const char *key, const char *what, uint32_t n_stock, bool fresh = false)
{
    static char t[120], path[FS_PATH_MAX];
    static uint64_t size;
    int i = c->settings ? c->settings->find(key) : -1;
    const char *p = i >= 0 ? c->settings->text(i) : "";
    if (fresh || strcmp(p, path) != 0) {
        bool dir = false;
        snprintf(path, sizeof(path), "%s", p);
        if (!p[0] || !c->fs || !c->fs->stat(p, &size, &dir) || dir) {
            size = 0;
            path[0] = 0;
        }
    }
    t[0] = 0;
    if (size == ROM_HALF && !n_stock) {
        snprintf(t, sizeof(t), "The half needs the stock %s ROM too: put it in roms/%s (USB stick or SD card)", what, c->desc->folder);
    }
    return t;
}

// The reply of a pick of a kit file: "selected, used from the next start" - or, when `note` (rom_half_note) has a text, that warning instead.
static inline void rom_pick_msg(char *msg, int n, const char *key, const char *file, const char *note)
{
    if (note[0]) {
        snprintf(msg, n, "%s: %s selected. %s", key, file, note);
    } else {
        snprintf(msg, n, "%s: %s selected, used from the next start with ROM set JiffyDOS", key, file);
    }
}

#endif
