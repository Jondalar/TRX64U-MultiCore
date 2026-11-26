// The diag core's engine v2, the app's side (dtv2.h).
#include "dtv2.h"
#include <string.h>

int dtv2_pins_read(CoreIo *io, DtPins *out)
{
    memset(out, 0, sizeof(*out));
    int n = io->rd(DV2_PIN_COUNT);
    if (n < 1 || n > DV2_PINS_MAX) {
        return -1;
    }
    for (int i = 0; i < n; i++) {
        io->wr(DV2_PIN_IDX, (uint8_t)i);
        DtPin &p = out->p[i];
        for (int k = 0; k < 15; k++) {
            p.name[k] = (char)io->rd(DV2_PIN_ROM + k);
        }
        p.name[15] = 0;
        p.dir = io->rd(DV2_PIN_ROM + 16);
        p.cls = io->rd(DV2_PIN_ROM + 17);
        p.flags = io->rd(DV2_PIN_ROM + 18);
        p.group = io->rd(DV2_PIN_ROM + 19);
        p.idle = io->rd(DV2_PIN_ROM + 20);
    }
    out->n = n;
    return 0;
}

int dtv2_pin_find(const DtPins &t, const char *name)
{
    for (int i = 0; i < t.n; i++) {
        if (!strcmp(t.p[i].name, name)) {
            return i;
        }
    }
    return -1;
}

bool dtv2_cap_read(CoreIo *io, uint32_t first_line, uint32_t n, uint32_t *out)
{
    uint32_t off = first_line * 16;
    for (int b = 0; b < 4; b++) {
        io->wr(DV2_CAP_RADDR + b, (uint8_t)(off >> (8 * b)));
    }
    uint8_t line[16];
    for (uint32_t i = 0; i < n; i++) {
        if ((i & 3) == 0) {
            io->wr(DV2_CAP_RCTRL, 1);                   // the address advances by 16 when the data is valid: the next line follows
            int tries = 0;
            while (!(io->rd(DV2_CAP_STATUS) & DV2_CAP_ST_VALID)) {
                if (++tries > 1000) {
                    return false;
                }
            }
            for (int k = 0; k < 16; k++) {
                line[k] = io->rd(DV2_CAP_RDATA + k);
            }
        }
        out[i] = dtv2_cap_decode(line, (int)(i & 3));
    }
    return true;
}
