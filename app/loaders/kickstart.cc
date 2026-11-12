#include "kickstart.h"

#define KS_MAGIC_256 0x1111u
#define KS_MAGIC_512 0x1114u

static uint16_t be16(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | p[1]);
}

int ks_header_version(const uint8_t *hdr, int len, uint16_t *ver, uint16_t *rev)
{
    if (!hdr || len < KS_HEAD) {
        return 0;
    }
    uint16_t magic = be16(hdr);
    if (magic != KS_MAGIC_256 && magic != KS_MAGIC_512) {
        return 0;
    }
    uint16_t v = be16(hdr + 12);
    if (v < 30 || v > 60) {              // 1.2 is 33, 3.2 is 47: anything outside is not a version
        return 0;
    }
    if (ver) {
        *ver = v;
    }
    if (rev) {
        *rev = be16(hdr + 14);
    }
    return 1;
}

int ks_pick(const KsCandidate *list, int n, uint16_t want)
{
    if (!list || n <= 0) {
        return 0;
    }
    for (int i = 0; i < n; i++) {
        if (list[i].ver == want) {
            return i;
        }
    }
    for (int i = 0; i < n; i++) {
        uint16_t v = list[i].ver;
        if (v == 0) {
            continue;
        }
        if (want >= KS_VER_AGA ? (v >= KS_VER_AGA) : (v <= KS_VER_13)) {
            return i;
        }
    }
    for (int i = 0; i < n; i++) {        // no machine of this family: any real Kickstart beats a blob
        if (list[i].ver != 0) {
            return i;
        }
    }
    return 0;
}
