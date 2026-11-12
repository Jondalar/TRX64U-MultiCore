#include "hidpad.h"
#include <string.h>

namespace {

struct Globals {
    uint32_t page;
    int32_t lmin, lmax;
    uint32_t size, count;
    uint8_t id;
};

struct IdBits {
    uint8_t id;
    uint32_t bits;
};

int32_t sext(uint32_t u, int size)
{
    if (size == 1) return (int8_t)u;
    if (size == 2) return (int16_t)u;
    return (int32_t)u;
}

// One pass over the descriptor. want_id < 0: find the report ID of the first button field inside a pad collection; otherwise fill l with the fields of that ID.
struct Pass {
    HidPadLayout *l;
    int want_id;
    int found_id;                // pass 1's answer, -1 none
    bool saw_ids;
    IdBits ids[8];
    int nids;
    uint32_t id_bits(uint8_t id) const
    {
        for (int i = 0; i < nids; i++) if (ids[i].id == id) return ids[i].bits;
        return 0;
    }
    void add_bits(uint8_t id, uint32_t n)
    {
        for (int i = 0; i < nids; i++) if (ids[i].id == id) { ids[i].bits += n; return; }
        if (nids < 8) { ids[nids].id = id; ids[nids].bits = n; nids++; }
    }
};

void set_field(HidField *f, uint32_t bit, uint32_t size, const Globals &g)
{
    if (f->ok || size == 0 || size > 32) return;                 // the first field of a usage wins
    f->ok = true;
    f->bit = (uint16_t)bit;
    f->size = (uint8_t)size;
    f->lmin = g.lmin;
    f->lmax = g.lmax;
    f->sign = g.lmin < 0;
}

void run_pass(const uint8_t *d, int len, Pass &p)
{
    Globals g;
    memset(&g, 0, sizeof(g));
    Globals stack[4];
    int sp = 0;
    uint32_t usages[32];
    int nus = 0;
    bool have_umin = false, have_umax = false;
    uint32_t umin = 0, umax = 0;
    int depth = 0;
    bool in_pad = false;
    p.nids = 0;
    p.saw_ids = false;
    int i = 0;
    while (i < len) {
        uint8_t b = d[i];
        if (b == 0xFE) {                                          // long item: skipped
            if (i + 1 >= len) break;
            i += d[i + 1] + 3;
            continue;
        }
        int size = b & 3;
        if (size == 3) size = 4;
        int type = (b >> 2) & 3, tag = b >> 4;
        if (i + 1 + size > len) break;
        uint32_t u = 0;
        for (int k = 0; k < size; k++) u |= (uint32_t)d[i + 1 + k] << (8 * k);
        int32_t s = sext(u, size);
        i += 1 + size;
        if (type == 1) {                                          // global
            switch (tag) {
            case 0: g.page = u; break;
            case 1: g.lmin = s; break;
            case 2: g.lmax = (g.lmin >= 0) ? (int32_t)u : s; break;     // 0x25 0xFF is 255 when the minimum is not negative
            case 7: g.size = u; break;
            case 8: g.id = (uint8_t)u; p.saw_ids = true; break;
            case 9: g.count = u; break;
            case 10: if (sp < 4) stack[sp++] = g; break;
            case 11: if (sp > 0) g = stack[--sp]; break;
            default: break;
            }
        } else if (type == 2) {                                   // local
            switch (tag) {
            case 0: if (nus < 32) usages[nus++] = (size == 4) ? u : ((g.page << 16) | u); break;
            case 1: have_umin = true; umin = (size == 4) ? u : ((g.page << 16) | u); break;
            case 2: have_umax = true; umax = (size == 4) ? u : ((g.page << 16) | u); break;
            default: break;
            }
        } else if (type == 0) {                                   // main
            if (tag == 10) {                                      // collection
                if (depth == 0) {
                    uint32_t us = nus ? usages[0] : 0;
                    in_pad = (u == 1) && (us == 0x10004 || us == 0x10005);
                }
                depth++;
            } else if (tag == 12) {
                if (depth > 0 && --depth == 0) in_pad = false;
            } else if (tag == 8) {                                // input
                uint32_t total = g.size * g.count;
                uint32_t bit = p.id_bits(g.id);
                bool mine = in_pad && !(u & 1) && g.size > 0;
                if (mine && ((p.want_id < 0) || (p.want_id == g.id))) {
                    uint32_t page = g.page;
                    if (have_umin && have_umax && umax >= umin && umax - umin < 32) {   // a range expands to the usage list
                        nus = 0;
                        for (uint32_t us = umin; us <= umax; us++) usages[nus++] = us;
                    }
                    if (page == 9) {                              // buttons
                        if (g.size == 1 && g.count > 0) {
                            if (p.want_id < 0) {
                                if (p.found_id < 0) p.found_id = g.id;
                            } else if (p.l->nruns < HIDPAD_RUNS) {
                                HidBtnRun &r = p.l->runs[p.l->nruns++];
                                r.bit = (uint16_t)bit;
                                r.count = (uint8_t)(g.count > 32 ? 32 : g.count);
                                r.first = nus ? (uint8_t)(usages[0] & 0xFFFF) : 1;
                                if (!r.first) r.first = 1;
                            }
                        }
                    } else if (page == 1 && (u & 2) && p.want_id >= 0) {        // Variable
                        for (uint32_t k = 0; k < g.count; k++) {
                            uint32_t us = nus ? (k < (uint32_t)nus ? usages[k] : usages[nus - 1]) : 0;
                            uint32_t fb = bit + k * g.size;
                            switch (us) {
                            case 0x10030: set_field(&p.l->x, fb, g.size, g); break;
                            case 0x10031: set_field(&p.l->y, fb, g.size, g); break;
                            case 0x10039: set_field(&p.l->hat, fb, g.size, g); break;
                            case 0x10090: set_field(&p.l->dp[0], fb, g.size, g); break;
                            case 0x10091: set_field(&p.l->dp[1], fb, g.size, g); break;
                            case 0x10093: set_field(&p.l->dp[2], fb, g.size, g); break;    // left
                            case 0x10092: set_field(&p.l->dp[3], fb, g.size, g); break;    // right
                            default: break;
                            }
                        }
                    }
                }
                p.add_bits(g.id, total);
            }
            nus = 0;
            have_umin = have_umax = false;
        }
    }
}

uint32_t get_bits(const uint8_t *d, uint32_t bit, uint32_t size)
{
    uint32_t v = 0;
    for (uint32_t k = 0; k < size; k++) {
        uint32_t bb = bit + k;
        v |= (uint32_t)((d[bb >> 3] >> (bb & 7)) & 1) << k;
    }
    return v;
}

bool field_val(const HidField &f, const uint8_t *d, int nbits, int32_t *v)
{
    if (!f.ok || (int)f.bit + f.size > nbits) return false;
    uint32_t u = get_bits(d, f.bit, f.size);
    if (f.sign && f.size < 32 && (u & (1u << (f.size - 1)))) u |= ~0u << f.size;
    *v = (int32_t)u;
    return true;
}

}

bool hidpad_parse(const uint8_t *desc, int len, HidPadLayout *l)
{
    memset(l, 0, sizeof(*l));
    Pass p;
    memset(&p, 0, sizeof(p));
    p.l = l;
    p.want_id = -1;
    p.found_id = -1;
    run_pass(desc, len, p);
    if (p.found_id < 0) return false;
    p.want_id = p.found_id;
    run_pass(desc, len, p);
    l->has_ids = p.saw_ids;
    l->report_id = (uint8_t)p.found_id;
    l->bits = (uint16_t)p.id_bits((uint8_t)p.found_id);
    uint32_t nb = 0;
    for (int i = 0; i < l->nruns; i++) nb += l->runs[i].count;
    bool dirs = l->hat.ok || (l->x.ok && l->y.ok) || (l->dp[0].ok && l->dp[1].ok && l->dp[2].ok && l->dp[3].ok);
    uint32_t need = 0;
    for (int i = 0; i < l->nruns; i++) if ((uint32_t)l->runs[i].bit + l->runs[i].count > need) need = (uint32_t)l->runs[i].bit + l->runs[i].count;
    const HidField *fs[7] = { &l->hat, &l->x, &l->y, &l->dp[0], &l->dp[1], &l->dp[2], &l->dp[3] };
    for (int i = 0; i < 7; i++) if (fs[i]->ok && (uint32_t)fs[i]->bit + fs[i]->size > need) need = (uint32_t)fs[i]->bit + fs[i]->size;
    l->need = (uint16_t)need;
    l->valid = nb >= 2 && dirs;
    return l->valid;
}

bool hidpad_decode(const HidPadLayout &l, const uint8_t *data, int len, PadRaw *out)
{
    if (!l.valid || len < 1) return false;
    const uint8_t *d = data;
    if (l.has_ids) {
        if (data[0] != l.report_id) return false;
        d = data + 1;
        len -= 1;
    }
    int nbits = len * 8;
    if (nbits < l.need) return false;                          // a report that does not reach the last field is not decoded half
    PadRaw r;
    r.buttons = 0;
    r.dpad = 0;
    for (int i = 0; i < l.nruns; i++) {
        const HidBtnRun &run = l.runs[i];
        if ((int)run.bit + run.count > nbits) return false;
        for (int k = 0; k < run.count; k++) {
            unsigned n = run.first + k - 1;
            if (n < 32 && ((d[(run.bit + k) >> 3] >> ((run.bit + k) & 7)) & 1)) r.buttons |= 1u << n;
        }
    }
    int32_t v;
    if (field_val(l.hat, d, nbits, &v)) {
        int32_t pos = v - l.hat.lmin;
        int32_t span = l.hat.lmax - l.hat.lmin;
        if (pos >= 0 && pos <= span && (span == 7 || span == 3)) {       // out of range is the null state: released
            int dir = (span == 7) ? (int)pos : (int)pos * 2;             // 0 N, 1 NE, 2 E, 3 SE, 4 S, 5 SW, 6 W, 7 NW
            if (dir == 7 || dir == 0 || dir == 1) r.dpad |= HIDPAD_DPAD_UP;
            if (dir >= 3 && dir <= 5) r.dpad |= HIDPAD_DPAD_DOWN;
            if (dir >= 5 && dir <= 7) r.dpad |= HIDPAD_DPAD_LEFT;
            if (dir >= 1 && dir <= 3) r.dpad |= HIDPAD_DPAD_RIGHT;
        }
    }
    static const uint8_t dpbit[4] = { HIDPAD_DPAD_UP, HIDPAD_DPAD_DOWN, HIDPAD_DPAD_LEFT, HIDPAD_DPAD_RIGHT };
    for (int i = 0; i < 4; i++) {
        if (field_val(l.dp[i], d, nbits, &v) && v != 0) r.dpad |= dpbit[i];
    }
    int32_t ax, ay;
    if (field_val(l.x, d, nbits, &ax) && field_val(l.y, d, nbits, &ay)) {
        // centre and half range from the logical limits, in 1/2 units so that an odd span stays exact
        int32_t cx2 = l.x.lmin + l.x.lmax, hx2 = l.x.lmax - l.x.lmin;
        int32_t cy2 = l.y.lmin + l.y.lmax, hy2 = l.y.lmax - l.y.lmin;
        int32_t dx = ax * 2 - cx2, dy = ay * 2 - cy2;               // doubled, as the half range is hx2 / 2 * 2
        int32_t tx = (hx2 * HIDPAD_DEADZONE_256) >> 8, ty = (hy2 * HIDPAD_DEADZONE_256) >> 8;
        if (dx < -tx) r.dpad |= HIDPAD_DPAD_LEFT;
        if (dx > tx) r.dpad |= HIDPAD_DPAD_RIGHT;
        if (dy < -ty) r.dpad |= HIDPAD_DPAD_UP;                     // HID: Y grows downward
        if (dy > ty) r.dpad |= HIDPAD_DPAD_DOWN;
    }
    *out = r;
    return true;
}
