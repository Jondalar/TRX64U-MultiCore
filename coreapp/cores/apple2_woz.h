// WOZ 1 and 2 disk images for the Apple //e's Disk II (T89.1 D7): the header is parsed once (INFO, TMAP, TRKS), a track is read per
// quarter-track request, a written track goes back to its slot in place. Pure apart from the CoreFile it is given.
// Format: https://applesaucefdc.com/woz/reference2/ (read by us; the numbers below are the file layout, not text of it).
#ifndef COREAPP_APPLE2_WOZ_H
#define COREAPP_APPLE2_WOZ_H

#include "fsif.h"
#include "apple2_nib.h"

struct A2Woz {
    CoreFile *f;
    uint8_t version;               // 1 or 2 (the magic: WOZ1, WOZ2)
    uint8_t tmap[160];             // quarter track -> TRKS entry, 0xFF: none (the head reads noise)
    uint32_t trks_off;             // file offset of the TRKS chunk's data
    uint32_t trks_size;
    bool write_protect;            // INFO byte 2
    bool crc_cleared;              // the header CRC field was set to 0 (first write)
};

// Opens the header. 0, or a reason in msg: not WOZ, a 3.5" disk, WOZ 3 / a FLUX chunk (refused, T89.1 Q1), a file with no TRKS, a truncated chunk.
int a2w_open(CoreFile *f, A2Woz *w, char *msg, int msglen);
// The track for quarter track q (0..159): its bits into `bits` (A2N_BITS_MAX / 8 bytes, the tail cleared), the count, noise = no track there.
// False when the file could not be read (the caller shows a notice).
bool a2w_track(A2Woz *w, int q, uint8_t *bits, uint32_t *nbits, bool *noise);
// The bits of quarter track q back into its slot (bytes of nbits, rounded up). False when q has no track or the write failed. The header's CRC
// field is set to 0 ("not computed") on the first write, as the format allows.
bool a2w_save(A2Woz *w, int q, const uint8_t *bits, uint32_t nbits);

#endif
