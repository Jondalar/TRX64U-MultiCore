// The ROM-set kind of entry (T104 5.1, 5.2 `rom_set`, 5.7): a core's ROM is not one named file but one of the files in a directory. The Amiga's
// Kickstart: the first directory (the board's, then the roots) that holds any file with the extension wins; the files of an allowed size are
// sorted by name, at most `max` of them, and become the value list of a menu setting ("Kickstart file") whose first value is Auto. Auto reads
// the version out of the image (a format of the loader library, here app/loaders/kickstart.h) and takes the machine's. The chosen member is
// written into the core's DDR2 window (a member of mirror_size twice, mirror_stride apart); on a reload the RAM below it is cleared first, so a
// cold start leaves nothing of the last run. A plain reset reloads when the choice names another file than the one in the window
// (reload_on_change). Pure: CoreFs and CoreIo only.
#ifndef COREAPP_ROMSET_H
#define COREAPP_ROMSET_H

#include "ctx.h"
#include "romfind.h"
#include "kickstart.h"
#include "romsetspec.h"

struct RomSetMember {
    char name[ROMSET_NAME];
    KsCandidate c;
};

struct RomSetState {
    RomSetMember m[ROMSET_MAX];
    int count;
    char dir[96];                         // the directory the list comes from
    char loaded[ROMSET_NAME];             // the file now in the window
    bool scanned;
    int chosen;                           // the member romset_hit() handed to the loader: the one now going into the window
};

// Looks for the set. ROM_FOUND with the list filled and the setting's labels written; ROM_MISSING when no directory holds a file with the
// extension, ROM_WRONG_SIZE when one does but none of an allowed size. summary gets the sentence. The settings are read again afterwards
// (the .cfg names a file that was not in the list before).
RomStatus romset_scan(CoreCtx *c, char *summary, int sumlen);

// The index of the member the choice names, or Auto's pick. -1: no list.
int romset_choice(CoreCtx *c);

// The choice names another file than the one in the window.
bool romset_stale(CoreCtx *c);

// Zeroes the machine's RAM below the ROM (the window range [0, clear_below)).
void romset_clear(CoreCtx *c);

// The chosen member's file: dir, name and size for the generic loader of boot.cc, which reads it and calls "romset.put" with the bytes.
bool romset_hit(CoreCtx *c, RomHit *hit);

// "romset.put": the bytes into the window, read back and compared. 0 or a reason.
int romset_put(CoreCtx *c, const uint8_t *data, uint32_t size, char *msg, int msglen);

#endif
