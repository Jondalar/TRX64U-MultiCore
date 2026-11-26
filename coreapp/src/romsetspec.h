// The data of a ROM-set entry (T104 5.1, 5.2 `rom_set`): the labels and the spec a descriptor holds, apart from the run-time state of romset.h (which needs the Kickstart
// format of app/loaders). T239.3 D17: descrec.h holds one in a description record and the host application includes it, so this file includes nothing of the loaders.
#ifndef COREAPP_ROMSETSPEC_H
#define COREAPP_ROMSETSPEC_H

#include <stdint.h>

struct KsCandidate;                      // app/loaders/kickstart.h: only pointers to it here

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

#endif
