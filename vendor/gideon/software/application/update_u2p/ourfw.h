// T135 B1/B2: which firmware in the flash is ours (a MultiCore host or the TRX64 monolith) and which one is somebody else's.
// A bitstream that writes the USR_ACCESS register is not enough: a self-built firmware (GitHub issue #6, "1.1.1+pkrl") can write it too and
// was taken for ours, so the installer made no backup of it. Ours = the bitstream writes AXSS AND the application in the flash names TRX64 (every
// application we ship does: the host app, the core app, the TRX64 Ultimate app; none of Commodore's or Gideon's files has the word).
// The updater (vendor/gideon/software/application/update_u2p/ourfw.h, the same file: tests/hostapp/run.sh compares them) and the host app's
// restore (fwrestore.cc) both ask here. Header only, no allocation, no library call: it builds in both trees.
#ifndef HOST_OURFW_H
#define HOST_OURFW_H

#include <stdint.h>

#define OURFW_MARK "TRX64"
#define OURFW_MARK_LEN 5

// The word is found in a stream fed in pieces (the updater reads the flash in chunks): *st is the state, start it at 0. True once the word has been seen.
static inline bool ourfw_scan_feed(int *st, const uint8_t *p, uint32_t n)
{
    static const char mark[OURFW_MARK_LEN + 1] = OURFW_MARK;
    for (uint32_t i = 0; i < n; i++) {
        if (p[i] == (uint8_t)mark[*st]) {
            if (++*st == OURFW_MARK_LEN) {
                return true;
            }
        } else {
            *st = (p[i] == (uint8_t)mark[0]) ? 1 : 0;       // "TRX64" has no border: a mismatch restarts at most at its first letter
        }
    }
    return false;
}

static inline bool ourfw_app_marked(const uint8_t *p, uint32_t n)
{
    int st = 0;
    return ourfw_scan_feed(&st, p, n);
}

// bit_axss: the bitstream writes the AXSS register; app_marked: the application names TRX64.
static inline bool ourfw_is_ours(bool bit_axss, bool app_marked)
{
    return bit_axss && app_marked;
}

// B2: the updater runs as a guest when a host started it: the hand-over page (handover.h, at 0x03EFF000) is valid. Then the running firmware is
// in the DDR2, not in the flash, and a backup of the flash would be a backup of the host. `page` = the 4 KiB page; the layout is struct Handover's
// version-1 part: magic, version, sum, video, path[256], core_name[48], core_version[24], host_build[16]; the sum is handover_sum()'s.
// tests/hostapp/test_ourfw.cc builds pages with handover_fill() and holds this function to handover_valid().
#define OURFW_HO_MAGIC   0x48585254u
#define OURFW_HO_VERSION 1u
#define OURFW_HO_SUMEND  360u        // offsetof(Handover, id_sum)

static inline uint32_t ourfw_ho_le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

static inline bool ourfw_page_valid(const uint8_t *page)
{
    if (ourfw_ho_le32(page) != OURFW_HO_MAGIC || ourfw_ho_le32(page + 4) != OURFW_HO_VERSION) {
        return false;
    }
    uint32_t s = 0x1F3D5B79u;
    for (uint32_t i = 12; i < OURFW_HO_SUMEND; i++) {
        s = ((s << 5) | (s >> 27)) ^ page[i];
    }
    return s == ourfw_ho_le32(page + 8) && page[16 + 255] == 0 && page[272 + 47] == 0 && page[320 + 23] == 0 && page[344 + 15] == 0;
}

#endif
