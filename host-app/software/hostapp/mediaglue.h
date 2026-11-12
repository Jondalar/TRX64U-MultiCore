// The media of the board as the File Browser and the format see them (T111): the FileManager's devices, found by the name the root
// directory lists. The board half; the render bench (tests/hostapp/render) supplies fake media with the same functions, tests/hostapp/test_exfatfmt
// drives fmt_exfat() itself over the same ops this file makes.
#ifndef HOST_MEDIAGLUE_H
#define HOST_MEDIAGLUE_H

#include <stdint.h>
#include "mediasniff.h"
#include "fmt.h"

struct MediaInfo {
    char name[24];
    bool found;               // a device of that name exists
    bool formattable;         // a removable medium: SD or USB, never the internal flash drive (O8)
    bool ready;               // a medium is in and answers
    uint32_t sectors, sector_size;
    MediaSniff sn;            // valid when ready
    const char *refuse;       // why a format would be refused now (NULL = it would not): fmt_check on the live device and the live heap
};

// Reads the sectors that tell what is on the medium (sector 0, a boot sector, a few of the root directory): a handful of reads.
bool media_info(const char *name, MediaInfo *mi);

// Is the medium still there (the device is ready). Cheap; the confirmation asks it a few times a second.
bool media_present(const char *name);

// The free space of the mounted volume, in bytes (reads the allocation bitmap or the FAT: not for every selection move, once after a format).
bool media_free_bytes(const char *name, uint64_t *bytes);

// Free heap in bytes (xPortGetFreeHeapSize).
uint32_t media_free_heap(void);

// The format of D4, on the medium of that name. why: the refusal or the failure in words.
FmtResult media_format(const char *name, FmtProgressCb cb, void *ctx, char *why, int whylen);

#endif
