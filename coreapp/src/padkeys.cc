// The pad-keys library (T216 P4, GitHub #80; padkeys.h): the test of a setting row for the pad-key shape and the learning of a key by pressing it. The generic application reaches both through the
// table "padkeys" (padkeys_api.h, padkeys_exports.cc).
#include "padkeys.h"

bool padkey_row(const Setting &s)
{
    return s.kind == 0 && s.nvalues == PADKEY_COUNT && s.bind && s.bind->bytes && s.bind->mask == 0xFF && memcmp(s.bind->bytes, padkey_codes, sizeof(padkey_codes)) == 0;
}

int padkey_learn_value(uint64_t down, char *line, int n)
{
    const uint64_t shifts = (1ull << 15) | (1ull << 52);
    const bool shift = (down & shifts) != 0;
    uint64_t keys = down & ~shifts;
    line[0] = 0;
    int nk = 0, k = -1;
    for (int i = 0; i < 64; i++) {
        if ((keys >> i) & 1) {
            nk++;
            k = i;
        }
    }
    if (nk == 0) {
        snprintf(line, n, "No key");
        return -1;
    }
    if (nk != 1) {
        snprintf(line, n, "Press one key");
        return -1;
    }
    for (int pass = 0; pass < 2; pass++) {                           // SHIFT + a cursor key is the other cursor key (code | 0x40); else SHIFT is ignored
        const int want = k | ((pass == 0 && shift) ? 0x40 : 0);
        for (int i = 1; i < PADKEY_COUNT; i++) {
            if (padkey_codes[i] == want) {
                return i;
            }
        }
    }
    snprintf(line, n, "%s", PADKEY_NOT_AVAILABLE);
    return -1;
}
