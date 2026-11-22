#include "libmenu.h"
#include <stdio.h>
#include <string.h>

// An application's route is its own screens: every area of it is the one named APP, never one of the core's (F1 .. F8).
static bool only_app_area(const DescRecord &d)
{
    if (d.skel.nareas < 1) {
        return false;
    }
    for (int i = 0; i < d.skel.nareas; i++) {
        if (!d.skel.areas[i].key || strcmp(d.skel.areas[i].key, "APP")) {
            return false;
        }
    }
    return true;
}

int libmenu_read(LibSource *src, uint32_t fsize, uint32_t off, DescRecord **rec, uint8_t **bytes, char *why, int whylen, bool app)
{
    *rec = 0;
    *bytes = 0;
    uint8_t h[16];
    if ((off & 3) || off > fsize || fsize - off < sizeof(h) || !src->read(off, h, sizeof(h))) {
        snprintf(why, whylen, "its menu record lies outside the file");
        return -1;
    }
    uint32_t total = h[8] | h[9] << 8 | h[10] << 16 | (uint32_t)h[11] << 24;
    if (total < DESCREC_HEADER || total > DESCREC_MAX_SIZE || total > fsize - off) {
        snprintf(why, whylen, "its menu record has the length %u, outside the file or above %u", (unsigned)total, DESCREC_MAX_SIZE);
        return -1;
    }
    uint8_t *b = new uint8_t[total];
    DescRecord *d = new DescRecord;
    char w[160];
    if (!src->read(off, b, total)) {
        snprintf(why, whylen, "its menu record cannot be read");
    } else if (descrec_parse(b, total, d, w, sizeof(w)) != 0) {
        snprintf(why, whylen, "%s", w);
    } else if ((d->has_route && !(app && only_app_area(*d))) || d->ov.nhide || d->ov.nrename || d->nkeys || d->nwindows) {
        snprintf(why, whylen, "its menu record gives a route, hides or renames what others contribute, or has a key table or windows; only a core's description may");
    } else {
        *rec = d;
        *bytes = b;
        return 0;
    }
    delete d;
    delete[] b;
    return -1;
}
