// ATR and XFD disk images read and written through a CoreFile (T93 D6). Pure.
#ifndef COREAPP_ATARI_ATR_H
#define COREAPP_ATARI_ATR_H

#include "atari_disk.h"
#include "fsif.h"

// The image format of an ATR header (16 bytes): 0x96 0x02, paragraphs (size/16) low word, sector size, paragraphs high byte.
// Returns the disk or null with the reason in err. The CoreFs keeps the file open until the disk is deleted.
AtariDisk *atr_open(CoreFs *fs, const char *path, char *err, int errlen);

// Pure helpers the tests check against hand-computed numbers.
struct AtrGeometry {
    bool ok;
    uint32_t header;            // 16 for an ATR, 0 for an XFD
    uint16_t sector_size;       // 128 or 256
    uint32_t sectors;
    bool packed_boot;           // ATR double density: the first three sectors are 128 bytes, packed
};
AtrGeometry atr_geometry(const uint8_t *head16, uint32_t file_size, char *err, int errlen);
uint32_t atr_sector_offset(const AtrGeometry &g, uint32_t n);    // byte offset in the file, 0 if n is out of range

#endif
