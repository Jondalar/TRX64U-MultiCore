// "Create blank ..." for every core with a drive that is not the 1541 (T122): the image builders - small pure functions, tested byte for byte
// against what each machine's own DOS reads - and the one operation `disk.create` that places the image (newfile.h) in the folder the menu
// is on. The 1541 library keeps its own D64 / G64 / D81 builders (app/lib/cbm1541/d1541_gcr.cc) and its own operation `drive.create`.
//
// Formatted where possible (T122 D1): every image is usable at once by the machine's DOS and holds no file.
#ifndef COREAPP_NEWDISK_H
#define COREAPP_NEWDISK_H

#include <stdint.h>
#include "ops.h"

enum NdFormat { ND_APPLE_DSK = 0, ND_ADF, ND_ATR, ND_CPC_DSK, ND_COUNT };

#define ND_APPLE_BYTES 143360u        // 35 tracks of 16 sectors of 256 bytes, DOS 3.3 order
#define ND_ADF_BYTES   901120u        // 80 tracks of 2 heads of 11 sectors of 512 bytes
#define ND_ATR_BYTES   92176u         // 16 byte header, then 720 sectors of 128 bytes
#define ND_CPC_BYTES   194816u        // 256 byte header, then 40 tracks of 0x1300 bytes (Track-Info and 9 sectors of 512)

const char *nd_ext(int fmt);                                    // the extension the image gets, ".dsk" ".adf" ".atr" ".dsk"
uint32_t nd_bytes(int fmt);                                     // the size of the image, 0 for an unknown format
// Builds the image into `out`. `name` is the volume name of a format that has one (the Amiga's); the others ignore it. Returns the size, 0 when
// `cap` is too small or the format is unknown.
uint32_t nd_build(int fmt, uint8_t *out, uint32_t cap, const char *name);

uint32_t nd_apple_dos33(uint8_t *out, uint32_t cap);            // VTOC T17 S0, catalog T17 S15 .. S1, tracks 0-2 and 17 in use: a data disk, not bootable
uint32_t nd_adf_ofs(uint8_t *out, uint32_t cap, const char *name);   // OFS volume: boot block, root block 880, bitmap block 881
uint32_t nd_atr_dos2(uint8_t *out, uint32_t cap);               // single density, Atari DOS 2.0S data disk: VTOC 360, directory 361-368
uint32_t nd_cpc_data(uint8_t *out, uint32_t cap);               // standard DSK, 40 tracks, 1 side, 9 sectors of 512 bytes C1-C9, filled with E5

// Registers `disk.create` (path, format, name) and the states `disk.suggest.<apple2_dsk|adf|atr|cpc_dsk>` under the contributor "newdisk".
// Call it last in a core's register_ops, as cbm1541_register().
void newdisk_register(OpRegistry &r);

#endif
