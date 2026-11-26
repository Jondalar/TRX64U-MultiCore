// T265 P2: what the recovery program takes for a host application (hostrecovery.app): header-only and free of every dependency, so the recovery program
// (host-app/software/hostrecovery/recovery.cc) and tests/hostapp/test_guard.cc run the very same rule. The rule is the one fw_image_seal_parts takes from the boot
// ROM (tools/trx_pkg.py check_app_record) plus: the file is exactly the record and its data padded to 4, nothing behind it.
#ifndef HOST_GUARDIMG_H
#define HOST_GUARDIMG_H

#include <stdint.h>

// the numbers the recovery program shares with the application (guard.h, flashfw.h); guard.cc asserts they are the same
#define GUARDIMG_BLK_ADDR        0x0000FF60u      // GUARD_BLK_ADDR: the recovery block is its first 8 words
#define GUARDIMG_RECOVERY_MAGIC  0x31564352u      // GUARD_RECOVERY_MAGIC "RCV1"
#define GUARDIMG_RECORD_ADDR     0x0021F000u      // FW_RECORD_ADDR
#define GUARDIMG_RECORD_MAGIC    0x47585254u      // FW_GUARD_MAGIC "TRXG"
#define GUARDIMG_APP_SLOT        0x001E0000u      // FW_APP_SLOT

static inline uint32_t guardimg_le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// hdr: the first 12 bytes of the file, file_size: its length. true and *total = 12 + padded length when it is an application the host may start.
static inline bool guardimg_ok(const uint8_t *hdr, uint64_t file_size, uint32_t *total)
{
    const uint32_t slot = GUARDIMG_APP_SLOT;
    uint32_t dest = guardimg_le32(hdr), len = guardimg_le32(hdr + 4), run = guardimg_le32(hdr + 8);
    if (len == 0 || len > slot - 12u || (dest & 3) || dest < 0x10000u || dest > 0x04000000u - len || run < dest || run >= dest + len) {
        return false;
    }
    uint32_t t = 12u + ((len + 3u) & ~3u);
    if (file_size != t) {
        return false;
    }
    *total = t;
    return true;
}

#endif
