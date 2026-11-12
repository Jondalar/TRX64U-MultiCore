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

#define ROMSET_MAX 8
#define ROMSET_NAME 48
#define ROMSET_SIZES 3

// The storage of the menu setting's value list: the label texts and the pointer array the Setting's `values` points at. Item 0 is the
// fixed Auto text, 1..max the members found ("-" for an unused place). The descriptor owns it (the Setting is const data, the labels are not).
struct RomSetLabels {
    char text[ROMSET_MAX + 1][ROMSET_NAME];
    const char *item[ROMSET_MAX + 1];
};

// The labels as the data of a descriptor: Auto's text first, then "-" for each unused place, the pointers into the same object. The Setting
// that chooses is built from `var.item` and ROMSET_MAX + 1 values.
#define ROMSET_LABELS(var, autotext) \
    RomSetLabels var = { { autotext, "-", "-", "-", "-", "-", "-", "-", "-" }, \
                         { var.text[0], var.text[1], var.text[2], var.text[3], var.text[4], var.text[5], var.text[6], var.text[7], var.text[8] } }

struct RomSetSpec {
    const char *role;                    // "Kickstart", for the screen
    const char *ext;                      // ".rom"
    uint32_t sizes[ROMSET_SIZES];         // the allowed sizes, 0-terminated
    const char *size_text;                // "256K or 512K", for the message
    const char *example;                  // "kick13.rom"
    int max;                              // at most ROMSET_MAX files
    const char *choice_key;               // the Setting that chooses: nvalues = max + 1, values = labels->item
    RomSetLabels *labels;
    const char *auto_label;               // "Auto (1.3 first)"
    uint16_t want;                        // the version Auto looks for (KS_VER_13 or KS_VER_31)
    // the format: reads the version out of a file's first bytes, ranks the candidates (null: Auto is the first file)
    int (*version)(const uint8_t *head, int len, uint16_t *ver, uint16_t *rev);
    int (*pick)(const KsCandidate *list, int n, uint16_t want);
    uint8_t head_bytes;                   // KS_HEAD
    // where it goes
    uint32_t window_off;                  // the DDR2 window offset
    uint32_t mirror_size, mirror_stride;  // a member of exactly mirror_size is written twice, mirror_stride apart (0: never)
    uint32_t clear_below;                 // DDR2 window bytes [0, clear_below) are the machine's RAM: cleared at the start and on a reload
};

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
