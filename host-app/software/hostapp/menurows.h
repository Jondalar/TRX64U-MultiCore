// T229: the boot menu's row data on the drive (Flash:/menu.rows), beside the picture (menusnap.h). Pure code: the record of one entry and the file
// that holds up to BOOT_MAX of them, with a format version, the host app's key and CRCs. Nothing here touches a file, LVGL or the menu's state;
// host_core.cc fills and takes the records, hostui.cc reads and writes the file.
#ifndef HOST_MENUROWS_H
#define HOST_MENUROWS_H

#include <stdint.h>
#include <stddef.h>

#define MR_PATH "/Flash/menu.rows"
#define MR_DIR "/Flash"
#define MR_FILE "menu.rows"
#define MR_ICON_PX 1024                 // the icon a row draws: 32 x 32 RGB565
#define MR_ICON_BYTES (MR_ICON_PX * 2)
#define MR_MAX 32                       // BOOT_MAX (bootcfg.h); the file never holds more
#define MR_QUIET_MS 2000                // the rows are unchanged for this long before the file is written (the picture's settle time, T146 D2)

// One entry as the menu shows it, and what makes the record still true (the boot.cfg line's own fields and the directory entry as read).
struct MrRec {
    char path[96];
    char hash[65];
    uint32_t size, mtime, hdr;          // boot.cfg's line: size, mtime and header digest stored at adding
    uint32_t dsize, dmtime;             // the directory entry at read time (what the background stat compares)
    uint8_t st;                         // EState
    uint8_t full;                       // a .cfw or a large .cor
    uint8_t large;                      // CorInfo::large
    uint8_t origin;                     // CorInfo::origin
    char name[48], shortn[8], version[24], id[24];
    uint32_t contract;
    int16_t cfgn;                       // config count (-1: not counted)
    uint32_t cfgstamp;                  // the config folders' directory mtimes the count was made under (0: none)
    uint8_t has_icon;
    const uint8_t *icon;                // MR_ICON_BYTES, little endian; points into the file image on decode; encode: the caller's bytes
};

enum MrResult { MR_OK = 0, MR_TOO_SHORT, MR_MAGIC, MR_FORMAT, MR_KEY, MR_LEN, MR_CRC };
const char *mr_result_text(MrResult r);

// Upper bound of an encoded file of n records.
uint32_t mr_max_encoded(int n);
// The file image of n records: header (magic, format, count, key, length, CRC of the rest) and per record a length, a CRC and the fields.
// Returns its length, -1 when it does not fit in cap.
int mr_encode(const MrRec *r, int n, uint32_t key, uint8_t *out, uint32_t cap);
// Takes a file image apart: out[] gets up to max records (icon pointers into the image). A record whose own CRC fails is skipped alone;
// a bad header or file CRC refuses everything. *n = records taken.
MrResult mr_decode(const uint8_t *file, uint32_t len, uint32_t key, MrRec *out, int max, int *n);
// The CRC the header holds (for "the same file again"); false when it is no header.
bool mr_file_crc(const uint8_t *file, uint32_t len, uint32_t *crc);

#endif
