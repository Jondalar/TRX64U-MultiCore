#include "descrec.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

namespace {

struct Rd {
    const uint8_t *p;
    uint32_t n;
    uint32_t str_off, str_size;
    char *why;
    int whylen;

    uint32_t u32(uint32_t off) const { return (uint32_t)p[off] | (uint32_t)p[off + 1] << 8 | (uint32_t)p[off + 2] << 16 | (uint32_t)p[off + 3] << 24; }
    int fail(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    // A string by offset: null for none, and sets bad when it does not lie in the table or does not end in it.
    const char *str(uint32_t o, bool *bad) const
    {
        if (o == DESCREC_NONE) {
            return 0;
        }
        if (o >= str_size || !memchr(p + str_off + o, 0, str_size - o)) {
            *bad = true;
            return 0;
        }
        return (const char *)(p + str_off + o);
    }
};

int Rd::fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(why, whylen, fmt, ap);
    va_end(ap);
    return -1;
}

struct Tab {
    uint32_t off, count;
};

bool id_ok(const char *s)
{
    if (!s || !s[0] || strlen(s) > 31) {
        return false;
    }
    for (; *s; s++) {
        if (!((*s >= 'a' && *s <= 'z') || (*s >= '0' && *s <= '9') || *s == '_')) {
            return false;
        }
    }
    return true;
}

}

// T239.3 D17: the data sections of a version 6 record. Every refusal names the record and the section.
static const char *const sec_name[DR_SECTIONS + 1] = { "", "core", "video", "roms", "setting register bits", "hotkeys", "mount slots" };

static int parse_sections(Rd &r, uint32_t off, uint32_t size, DescRecord *out, uint32_t ncset)
{
    uint32_t seen = 0;
    bool bad = false;
    uint32_t pos = off;
    const uint32_t end = off + size;
    while (pos < end) {
        if (end - pos < 8) {
            return r.fail("description record: a section header is cut off");
        }
        uint32_t id = r.u32(pos), len = r.u32(pos + 4);
        if (id < 1 || id > DR_SECTIONS) {
            return r.fail("description record, section %u: this application does not know it", (unsigned)id);
        }
        const char *nm = sec_name[id];
        if ((seen >> id & 1) || (len & 3) || len > end - pos - 8) {
            return r.fail("description record, section %s: twice, not a multiple of 4, or longer than the record", nm);
        }
        seen |= 1u << id;
        const uint32_t b = pos + 8;
        pos = b + len;
#define SW(i) r.u32(b + 4 * (uint32_t)(i))
#define NEED(words) do { if (len != 4 * (uint32_t)(words)) { return r.fail("description record, section %s: %u bytes, %u expected", nm, (unsigned)len, 4 * (unsigned)(words)); } } while (0)
        if (id == 1) {
            NEED(18);
            uint32_t idw = SW(0);
            for (int i = 0; i < 4; i++) {
                out->ident.id[i] = (char)(idw >> (8 * i));
            }
            out->ident.name = r.str(SW(1), &bad);
            out->ident.title = r.str(SW(2), &bad);
            out->ident.short_title = r.str(SW(3), &bad);
            out->ident.folder = r.str(SW(4), &bad);
            out->ident.store = r.str(SW(5), &bad);
            out->ident.min_contract = SW(6);
            out->ident.reset_ms = SW(7);
            out->ident.service_op = r.str(SW(8), &bad);
            out->ident.service_period_ms = SW(9);
            out->ident.flags = SW(10);
            out->ident.needs_program = r.str(SW(11), &bad);
            out->ident.program_ext = r.str(SW(12), &bad);
            out->ident.type_op = r.str(SW(13), &bad);
            out->ident.run_op = r.str(SW(14), &bad);
            out->ident.core_id = r.str(SW(15), &bad);
            out->ident.live_ms = SW(16);
            const char *caps = r.str(SW(17), &bad);
            if (bad || !out->ident.name || !out->ident.name[0] || !out->ident.title || !out->ident.title[0] || !out->ident.short_title || !out->ident.short_title[0] ||
                !out->ident.folder || !out->ident.folder[0] || !out->ident.store || !out->ident.store[0] || (out->ident.flags >> 6) || out->ident.min_contract > 255 ||
                out->ident.reset_ms > 65535 || out->ident.live_ms > 65535 || out->ident.service_period_ms > 60000 ||
                (out->ident.service_op == 0) != (out->ident.service_period_ms == 0) || (out->ident.service_op && !out->ident.service_op[0])) {
                return r.fail("description record, section %s: a text is missing or outside the table, a flag is not known, or a number is out of range", nm);
            }
            for (const char *p = caps; p && *p;) {
                const char *e = strchr(p, ',');
                size_t l = e ? (size_t)(e - p) : strlen(p);
                if (out->ncaps >= DR_CAPS || l < 1 || l > 15) {
                    return r.fail("description record, section %s: a capability name is empty, longer than 15 or one too many (%d)", nm, DR_CAPS);
                }
                memcpy(out->cap_names[out->ncaps], p, l);
                out->cap_names[out->ncaps][l] = 0;
                out->caps[out->ncaps] = out->cap_names[out->ncaps];
                out->ncaps++;
                p = e ? e + 1 : 0;
            }
            out->has_core = true;
        } else if (id == 2) {
            NEED(19);
            VideoDesc &v = out->video;
            memset(&v, 0, sizeof(v));
            v.w = SW(0); v.h = SW(1);
            uint32_t scale = SW(2), nbuf = SW(7), bpp = SW(12), pxfmt = SW(13), h60 = SW(14);
            v.x0 = (int32_t)SW(3); v.y0 = (int32_t)SW(4);
            v.fb_base = SW(5); v.fb_stride = SW(6);
            uint32_t reg[4] = { SW(8), SW(9), SW(10), SW(11) };
            FreezeDesc &f = out->freeze;
            memset(&f, 0, sizeof(f));
            uint32_t fr = SW(15), fm = SW(16), ft = SW(17), fw = SW(18);
            if (scale < 1 || scale > 3 || nbuf > 8 || (bpp != 0 && bpp != 2 && bpp != 4) || pxfmt > 4 || h60 > 65535 || reg[0] > 65535 || reg[1] > 65535 || reg[2] > 65535 ||
                reg[3] > 65535 || fr > 65535 || fm > 255 || ft > 65535 || fw > 1) {
                return r.fail("description record, section %s: a number is out of range", nm);
            }
            v.scale = (uint8_t)scale; v.nbuf = (uint8_t)nbuf; v.bpp = (uint8_t)bpp; v.pxfmt = (uint8_t)pxfmt; v.h60 = (uint16_t)h60;
            v.reg_latest = (uint16_t)reg[0]; v.reg_seq = (uint16_t)reg[1]; v.reg_fb_base = (uint16_t)reg[2]; v.reg_fb_stride = (uint16_t)reg[3];
            f.reg_status = (uint16_t)fr; f.mask = (uint8_t)fm; f.timeout_ms = (uint16_t)ft; f.word = fw != 0;
            out->has_video = true;
        } else if (id == 3) {
            if (len < 4) {
                return r.fail("description record, section %s: cut off", nm);
            }
            uint32_t n = SW(0);
            const uint32_t ew = 3 + ROM_SIZES_MAX + ROM_NAMES_MAX;
            if (n < 1 || n > ROMS_MAX || len != 4 + n * 4 * ew) {
                return r.fail("description record, section %s: %u entries, or the length does not fit them", nm, (unsigned)n);
            }
            for (uint32_t i = 0; i < n; i++) {
                const uint32_t o = 1 + i * ew;
                RomSpec &d = out->roms[i];
                memset(&d, 0, sizeof(d));
                d.role = r.str(SW(o), &bad);
                d.put_op = r.str(SW(o + 1), &bad);
                uint32_t fl = SW(o + 2);
                if (bad || !d.role || !d.role[0] || !d.put_op || !d.put_op[0] || (fl & ~0xFF03u)) {
                    return r.fail("description record, section %s: entry %u has a bad role, operation or flag", nm, (unsigned)i);
                }
                d.required = fl & 1;
                d.shared = (fl & 2) != 0;
                d.set = (uint8_t)(fl >> 8);
                int ns = 0;
                for (int k = 0; k < ROM_SIZES_MAX; k++) {
                    uint32_t z = SW(o + 3 + k);
                    if (z) {
                        if (ns != k) {
                            return r.fail("description record, section %s: entry %u has a size behind a gap", nm, (unsigned)i);
                        }
                        d.sizes[ns++] = z;
                    }
                }
                int nn = 0;
                for (int k = 0; k < ROM_NAMES_MAX; k++) {
                    uint32_t w = SW(o + 3 + ROM_SIZES_MAX + k);
                    if (w != DESCREC_NONE) {
                        if (nn != k) {
                            return r.fail("description record, section %s: entry %u has a name behind a gap", nm, (unsigned)i);
                        }
                        d.names[nn++] = r.str(w, &bad);
                        if (bad || !d.names[nn - 1][0]) {
                            return r.fail("description record, section %s: entry %u has an empty name or one outside the table", nm, (unsigned)i);
                        }
                    }
                }
                if (ns == 0 || nn == 0) {
                    return r.fail("description record, section %s: entry %u has no size or no name", nm, (unsigned)i);
                }
            }
            out->nroms = (int)n;
        } else if (id == 4) {
            if (len < 4) {
                return r.fail("description record, section %s: cut off", nm);
            }
            uint32_t n = SW(0), at = 1;
            if (n < 1 || n > DR_SETBINDS) {
                return r.fail("description record, section %s: %u entries (1 to %d)", nm, (unsigned)n, DR_SETBINDS);
            }
            for (uint32_t i = 0; i < n; i++) {
                if (len / 4 < at + 5) {
                    return r.fail("description record, section %s: entry %u is cut off", nm, (unsigned)i);
                }
                uint32_t idx = SW(at), reg = SW(at + 1), shift = SW(at + 2), mask = SW(at + 3), nb = SW(at + 4);
                uint32_t padw = (nb + 3) / 4;
                if (idx >= ncset || reg > 65535 || shift > 7 || mask < 1 || mask > 255 || nb > DR_BIND_BYTES || len / 4 < at + 5 + padw ||
                    out->csettings[idx].bind || (nb && nb != (uint32_t)out->csettings[idx].nvalues)) {
                    return r.fail("description record, section %s: entry %u names no core setting (or twice), or has a bad register, shift, mask or byte list", nm, (unsigned)i);
                }
                RegBind &d = out->setbind[i];
                d.reg = (uint16_t)reg;
                d.shift = (uint8_t)shift;
                d.mask = (uint8_t)mask;
                d.bytes = 0;
                if (nb) {
                    for (uint32_t k = 0; k < nb; k++) {
                        out->setbind_bytes[i][k] = r.p[b + 4 * (at + 5) + k];
                    }
                    d.bytes = out->setbind_bytes[i];
                }
                out->csettings[idx].bind = &d;
                at += 5 + padw;
            }
            if (at * 4 != len) {
                return r.fail("description record, section %s: the length does not fit its entries", nm);
            }
            out->nsetbind = (int)n;
        } else if (id == 5) {
            NEED(22);
            HkSpec &h = out->hot;
            memset(&h, 0, sizeof(h));
            uint32_t key[2 + HK_NACT + HK_TASKS_MAX];
            for (int i = 0; i < 2 + HK_NACT; i++) {
                key[i] = SW(i);
            }
            uint32_t nt = SW(2 + HK_NACT);
            for (int i = 0; i < HK_TASKS_MAX; i++) {
                key[2 + HK_NACT + i] = SW(3 + HK_NACT + i);
            }
            const uint32_t o = 3 + HK_NACT + HK_TASKS_MAX;
            h.vol_key = r.str(SW(o), &bad);
            h.swap_key = r.str(SW(o + 1), &bad);
            h.own_keys = (uint64_t)SW(o + 2) | (uint64_t)SW(o + 3) << 32;
            h.core_op = r.str(SW(o + 4), &bad);
            h.core_key = r.str(SW(o + 5), &bad);
            h.core_label = r.str(SW(o + 6), &bad);
            uint32_t pc = SW(o + 7);
            for (int i = 0; i < 2 + HK_NACT + HK_TASKS_MAX; i++) {
                if (key[i] > 64 && key[i] != HK_NONE) {
                    bad = true;
                }
            }
            if (bad || nt > HK_TASKS_MAX || pc > 1 || (h.core_op == 0) != (h.core_key == 0) || (h.core_op == 0) != (h.core_label == 0)) {
                return r.fail("description record, section %s: a key, the task count or a text is out of range", nm);
            }
            h.def.mod_a = (uint8_t)key[0];
            h.def.mod_b = (uint8_t)key[1];
            for (int i = 0; i < HK_NACT; i++) {
                h.def.act[i] = (uint8_t)key[2 + i];
            }
            h.def.ntask = (uint8_t)nt;
            for (int i = 0; i < HK_TASKS_MAX; i++) {
                h.def.task[i] = (uint8_t)key[2 + HK_NACT + i];
            }
            h.pc_keys = pc != 0;
            out->has_hot = true;
        } else {
            if (len < 4) {
                return r.fail("description record, section %s: cut off", nm);
            }
            uint32_t n = SW(0);
            if (n < 1 || n > DR_MOUNT || len != 4 + 4 * n) {
                return r.fail("description record, section %s: %u slots, or the length does not fit them", nm, (unsigned)n);
            }
            for (uint32_t i = 0; i < n; i++) {
                out->mount_names[i] = r.str(SW(1 + i), &bad);
                if (bad || !out->mount_names[i] || !out->mount_names[i][0] || strlen(out->mount_names[i]) > 9) {
                    return r.fail("description record, section %s: slot %u has no name, or one longer than 9", nm, (unsigned)i);
                }
            }
            out->nmount = (int)n;
        }
#undef SW
#undef NEED
    }
    if (!(seen & 2)) {
        return r.fail("description record, section %s: missing (the data sections need the core's identity)", sec_name[1]);
    }
    for (uint32_t i = 0; i < ncset; i++) {
        if (!out->csettings[i].apply && !out->csettings[i].bind) {
            return r.fail("description record, section %s: core setting %u has neither an operation nor register bits", sec_name[4], (unsigned)i);
        }
    }
    return 0;
}

int descrec_parse(const uint8_t *rec, uint32_t n, DescRecord *out, char *why, int whylen)
{
    Rd r = { rec, n, 0, 0, why, whylen };
    memset(out, 0, sizeof(*out));
    if (n < DESCREC_HEADER || n > DESCREC_MAX_SIZE) {
        return r.fail("description record: %u bytes (%u to %u are allowed)", (unsigned)n, DESCREC_HEADER, DESCREC_MAX_SIZE);
    }
    if (r.u32(0) != DESCREC_MAGIC) {
        return r.fail("description record: not a description (magic)");
    }
    uint32_t vh = r.u32(4);
    const uint32_t ver = vh & 0xFFFF;
    if (ver < DESCREC_VERSION_MIN || ver > DESCREC_VERSION) {         // version 2 is version 3 without a layout and attributes (those words were 0); 4 widens the media slot and gives the header's last word a use
        return r.fail("description record: version %u, this application reads %u to %u", (unsigned)ver, DESCREC_VERSION_MIN, DESCREC_VERSION);
    }
    const uint32_t hdr = ver >= 6 ? DESCREC_HEADER_V6 : ver >= 5 ? DESCREC_HEADER_V5 : DESCREC_HEADER;    // version 5 carries the pad's words behind the machine-info line, 6 the sections' place
    if ((vh >> 16) != hdr || r.u32(8) != n || n < hdr) {
        return r.fail("description record: the header or the length does not match the file");
    }
    uint32_t flags = r.u32(12);
    if ((flags & ~(ver >= 6 ? 7u : 3u)) || ((flags & 4) != 0) != (ver >= 6)) {
        return r.fail("description record: flags 0x%x are not known", (unsigned)flags);
    }
    r.str_off = r.u32(16);
    r.str_size = r.u32(20);
    if (r.str_off < hdr || (uint64_t)r.str_off + r.str_size > n || r.str_size == 0) {
        return r.fail("description record: the string table lies outside the record");
    }
    const uint32_t esize[DR_TABLES] = { 20, 24, 52, 40, 32, 4, 8, 16, 4, 4, 4, 24, 36, ver >= 4 ? 44u : 24u, 24, 36, 36 };
    static const uint32_t emax[DR_TABLES] = { DR_AREAS, DR_TABS, DR_NODES, DR_ACTIONS, DR_SETTINGS, DR_HIDE, DR_RENAME, DR_BINDS, DR_EXTS, DR_SIZES, DR_VALUES, DR_CTABS,
                                              DR_TASKS, DR_MEDIA, DR_FTYPES, DR_FACTS, DR_CSETS };
    static const char *const ename[DR_TABLES] = { "areas", "tabs", "nodes", "file actions", "settings", "hide list", "rename list", "bindings", "extensions",
                                                  "sizes", "values", "contributed tabs", "tasks", "media slots", "file types", "file type actions", "core settings" };
    Tab tb[DR_TABLES];
    for (int i = 0; i < DR_TABLES; i++) {
        tb[i].off = r.u32(24 + 8 * i);
        tb[i].count = r.u32(28 + 8 * i);
        if (tb[i].count > emax[i]) {
            return r.fail("description record: %u %s (this application takes %u)", (unsigned)tb[i].count, ename[i], (unsigned)emax[i]);
        }
        if (tb[i].count && (tb[i].off < hdr || (uint64_t)tb[i].off + (uint64_t)tb[i].count * esize[i] > n)) {
            return r.fail("description record: the table of %s lies outside the record", ename[i]);
        }
    }
    enum { A, T, N, F, S, H, R, B, E, Z, V, C, K, M, Y, X, Q };
    bool bad = false;
#define ENT(t, i) (r.p + tb[t].off + (uint32_t)(i) * esize[t])
#define W(e, k) r.u32((uint32_t)((e) - r.p) + 4 * (k))
    // bindings, extensions, sizes and values first: the entries that use them check their runs against these counts
    for (uint32_t i = 0; i < tb[B].count; i++) {
        const uint8_t *e = ENT(B, i);
        OpBind &b = out->binds[i];
        b.arg = r.str(W(e, 0), &bad);
        uint32_t kind = W(e, 1);
        b.kind = (uint8_t)kind;
        b.i = (int32_t)W(e, 2);
        b.s = r.str(W(e, 3), &bad);
        if (bad || !b.arg || !b.arg[0] || kind > BIND_TEXT || (kind == BIND_STR && !b.s)) {
            return r.fail("description record: binding %u has a bad name, kind or text", (unsigned)i);
        }
    }
    for (uint32_t i = 0; i < tb[E].count; i++) {
        out->exts[i] = r.str(W(ENT(E, i), 0), &bad);
        if (bad || !out->exts[i] || out->exts[i][0] != '.') {
            return r.fail("description record: extension %u does not start with a dot", (unsigned)i);
        }
    }
    for (uint32_t i = 0; i < tb[Z].count; i++) {
        out->sizes[i] = W(ENT(Z, i), 0);
    }
    for (uint32_t i = 0; i < tb[V].count; i++) {
        out->values[i] = r.str(W(ENT(V, i), 0), &bad);
        if (bad || !out->values[i] || !out->values[i][0]) {
            return r.fail("description record: value label %u is empty or outside the table", (unsigned)i);
        }
    }
    for (uint32_t i = 0; i < tb[H].count; i++) {
        out->hide[i] = r.str(W(ENT(H, i), 0), &bad);
        if (bad || !out->hide[i] || !out->hide[i][0]) {
            return r.fail("description record: hide entry %u is empty", (unsigned)i);
        }
    }
    for (uint32_t i = 0; i < tb[R].count; i++) {
        const uint8_t *e = ENT(R, i);
        out->rename[i].id = r.str(W(e, 0), &bad);
        out->rename[i].label = r.str(W(e, 1), &bad);
        if (bad || !out->rename[i].id || !out->rename[i].id[0] || !out->rename[i].label || !out->rename[i].label[0]) {
            return r.fail("description record: rename entry %u lacks an id or a label", (unsigned)i);
        }
    }
    // the poll, and the libraries whose menus were merged into this record when it was packed
    {
        uint32_t po = r.u32(24 + 8 * DR_TABLES), pms = r.u32(28 + 8 * DR_TABLES), mg = r.u32(32 + 8 * DR_TABLES);
        uint32_t mi = r.u32(36 + 8 * DR_TABLES);                      // version 4: the string offset of the machine-info line's settings (T228); before: 0
        if ((ver < 4 && mi != 0) || ((po == DESCREC_NONE) != (pms == 0)) || pms > 60000) {
            return r.fail("description record: the poll has a bad operation or period");
        }
        out->rec.poll.op = r.str(po, &bad);
        if (bad || (po != DESCREC_NONE && !out->rec.poll.op[0])) {
            return r.fail("description record: the poll has a bad operation or period");
        }
        out->rec.poll.period_ms = (uint16_t)pms;
        out->rec.poll.when_frozen = false;
        out->machine_info = ver >= 4 ? r.str(mi, &bad) : 0;
        if (bad || (ver >= 4 && mi != DESCREC_NONE && !out->machine_info[0])) {
            return r.fail("description record: the machine info is empty or outside the table");
        }
        out->merged = r.str(mg, &bad);
        if (bad || (mg != DESCREC_NONE && !out->merged[0])) {
            return r.fail("description record: the list of merged libraries is empty or outside the table");
        }
    }
    // T228 R10, version 5: the core's pad (pad.h PadSpec): the output and by_id, two registers (or ports), the button names (a run of the value table) and the
    // default map (two hex digits per button)
    if (ver >= 5 && r.u32(DESCREC_HEADER) != DESCREC_NONE) {
        uint32_t kind = r.u32(DESCREC_HEADER), r0 = r.u32(DESCREC_HEADER + 4), r1 = r.u32(DESCREC_HEADER + 8);
        uint32_t fv = r.u32(DESCREC_HEADER + 12), nv = r.u32(DESCREC_HEADER + 16);
        const char *def = r.str(r.u32(DESCREC_HEADER + 20), &bad);
        if (bad || (kind & ~0x1FFu) || (kind & 0xFF) > PAD_OUT_JOY || r0 > 0xFFFF || r1 > 0xFFFF || nv < 1 || nv > PAD_BUTTONS_MAX ||
            (uint64_t)fv + nv > tb[V].count || !def || strlen(def) != 2 * nv) {
            return r.fail("description record: the pad has a bad kind, register, run of button names or default map");
        }
        for (uint32_t i = 0; i < nv; i++) {
            unsigned v = 0;
            for (int k = 0; k < 2; k++) {
                char ch = def[2 * i + k];
                int d = (ch >= '0' && ch <= '9') ? ch - '0' : (ch >= 'A' && ch <= 'F') ? ch - 'A' + 10 : -1;
                if (d < 0) {
                    return r.fail("description record: the pad's default map is not hex");
                }
                v = v * 16 + (unsigned)d;
            }
            if (v > 35 && v != PAD_SRC_NONE && (v < PAD_ROLE_RIGHT || v > PAD_ROLE_START)) {
                return r.fail("description record: entry %u of the pad's default map is no source", (unsigned)i);
            }
            out->pad_def[i] = (uint8_t)v;
        }
        PadSpec &p = out->pad;
        memset(&p, 0, sizeof(p));
        p.reg[0] = (uint16_t)r0;
        p.reg[1] = (uint16_t)r1;
        p.nbuttons = (uint8_t)nv;
        p.names = out->values + fv;
        p.def_map = out->pad_def;
        p.by_id = (uint8_t)((kind >> 8) & 1);
        p.out = (uint8_t)(kind & 0xFF);
        out->has_pad = true;
    }
    // T228 R17, version 5: what "Restart core" clears (CoreDesc::cold_ram_bytes, cold_ram_only)
    if (ver >= 5) {
        uint32_t clear = r.u32(DESCREC_HEADER + 24), fl = r.u32(DESCREC_HEADER + 28);
        if ((fl & ~1u) || clear > DESCREC_RESTART_MAX || (clear & 0xFF) || (fl && !clear)) {
            return r.fail("description record: the restart clears a bad amount of RAM or has unknown flags");
        }
        out->restart_clear = clear;
        out->restart_ram_only = (fl & 1) != 0;
    }
    // contributed tabs
    for (uint32_t i = 0; i < tb[C].count; i++) {
        const uint8_t *e = ENT(C, i);
        LibTabDecl &d = out->ctabs[i];
        d.area = r.str(W(e, 0), &bad);
        d.id = r.str(W(e, 1), &bad);
        d.name = r.str(W(e, 2), &bad);
        uint32_t widget = W(e, 3), nn = W(e, 5);
        d.groups = r.str(W(e, 4), &bad);
        if (bad || !d.area || strlen(d.area) != 2 || d.area[0] != 'F' || d.area[1] < '1' || d.area[1] > '8' || !id_ok(d.id) || !d.name || !d.name[0] ||
            widget > TW_NODES || nn > 99) {
            return r.fail("description record: contributed tab %u has a bad area, id, name, widget or number", (unsigned)i);
        }
        if (d.groups && !d.groups[0]) {
            d.groups = 0;
        }
        d.widget = (uint8_t)widget;
        d.n = (uint8_t)nn;
    }
    out->rec.tabs = out->ctabs;
    out->rec.ntabs = (int)tb[C].count;
    // the route
    for (uint32_t i = 0; i < tb[T].count; i++) {
        const uint8_t *e = ENT(T, i);
        SkelTab &s = out->tabs[i];
        s.id = r.str(W(e, 0), &bad);
        s.name = r.str(W(e, 1), &bad);
        uint32_t nn = W(e, 2), widget = W(e, 3);
        s.groups = r.str(W(e, 4), &bad);
        uint32_t lo = W(e, 5);
        s.layout = lo ? r.str(lo, &bad) : 0;                              // 0: none (offset 0 is the empty string)
        if (s.name && !s.name[0]) {
            s.name = 0;                                                    // T205 D1: no name = named after the area (F5: the core)
        }
        if (bad || !id_ok(s.id) || nn > 99 || widget > TW_NODES || (s.layout && !s.layout[0])) {
            return r.fail("description record: tab %u has a bad id, name, number or widget", (unsigned)i);
        }
        s.n = (uint8_t)nn;
        s.widget = (uint8_t)widget;
        if (s.groups && !s.groups[0]) {
            s.groups = 0;
        }
        if (s.groups && widget != TW_SETTINGS && widget != TW_TASKS && widget != TW_MEDIA && widget != TW_INFO && widget != TW_SYSTEM) {      // a System tab's groups: its pages (T228 R6)
            return r.fail("description record: tab %u lists groups, but its widget takes none", (unsigned)i);
        }
    }
    uint32_t tab_used = 0;
    for (uint32_t i = 0; i < tb[A].count; i++) {
        const uint8_t *e = ENT(A, i);
        SkelArea &a = out->areas[i];
        a.key = r.str(W(e, 0), &bad);
        a.name = r.str(W(e, 1), &bad);
        uint32_t fl = W(e, 2), first = W(e, 3), cnt = W(e, 4);
        if (bad || !a.key || !((strlen(a.key) == 2 && a.key[0] == 'F' && a.key[1] >= '1' && a.key[1] <= '8') || !strcmp(a.key, "APP")) || (fl & ~1u)) {
            return r.fail("description record: area %u has no key F1 .. F8 (or APP, an application's own screens)", (unsigned)i);
        }
        for (uint32_t j = 0; j < i; j++) {
            if (!strcmp(out->areas[j].key, a.key)) {
                return r.fail("description record: area %s twice", a.key);
            }
        }
        if (first != tab_used || cnt == 0 || cnt > MENU_TABS_MAX || (uint64_t)first + cnt > tb[T].count) {
            return r.fail("description record: the tabs of area %s are out of place (an area lists its tabs in order, 1 to %d)", a.key, MENU_TABS_MAX);
        }
        tab_used += cnt;
        a.shifted = fl & 1;
        a.tabs = out->tabs + first;
        a.ntabs = (int)cnt;
        for (uint32_t t = 0; t < cnt; t++) {
            for (uint32_t u = 0; u < t; u++) {
                if (!strcmp(a.tabs[t].id, a.tabs[u].id) || (a.tabs[t].n && a.tabs[t].n == a.tabs[u].n)) {
                    return r.fail("description record: area %s lists tab \"%s\" or its number twice", a.key, a.tabs[t].id);
                }
            }
        }
    }
    if (tab_used != tb[T].count) {
        return r.fail("description record: %u tabs belong to no area", (unsigned)(tb[T].count - tab_used));
    }
    out->has_route = (flags & 1) != 0;
    if (out->has_route != (tb[A].count > 0)) {
        return r.fail("description record: the route flag and the area table disagree");
    }
    out->skel.areas = out->areas;
    out->skel.nareas = (int)tb[A].count;
    // nodes
    for (uint32_t i = 0; i < tb[N].count; i++) {
        const uint8_t *e = ENT(N, i);
        LibNodeDecl &d = out->nodes[i];
        d.anchor = r.str(W(e, 0), &bad);
        uint32_t kind = W(e, 1);
        d.kind = (uint8_t)kind;
        d.label = r.str(W(e, 2), &bad);
        d.op = r.str(W(e, 3), &bad);
        d.state = r.str(W(e, 4), &bad);
        d.order = (int16_t)(int32_t)W(e, 5);
        d.when_state = r.str(W(e, 6), &bad);
        d.capability = r.str(W(e, 7), &bad);
        d.id = r.str(W(e, 8), &bad);
        uint32_t fb = W(e, 9), nb = W(e, 10), fl = W(e, 11);
        uint32_t ao = W(e, 12);
        d.attr = ao ? r.str(ao, &bad) : 0;
        if (bad || !d.anchor || !d.label || kind > LN_GROUP || (fl & ~1u) || (d.attr && !d.attr[0]) || (int32_t)W(e, 5) < -32768 || (int32_t)W(e, 5) > 32767 ||
            (uint64_t)fb + nb > tb[B].count) {
            return r.fail("description record: node %u has a bad string, kind, order or binding run", (unsigned)i);
        }
        d.bind = nb ? out->binds + fb : 0;
        d.nbind = (int)nb;
        d.closes_menu = fl & 1;
    }
    // file actions
    for (uint32_t i = 0; i < tb[F].count; i++) {
        const uint8_t *e = ENT(F, i);
        LibFileActionDecl &d = out->actions[i];
        d.label = r.str(W(e, 0), &bad);
        d.op = r.str(W(e, 1), &bad);
        uint32_t fe = W(e, 2), ne = W(e, 3), fz = W(e, 4), nz = W(e, 5), fb = W(e, 6), nb = W(e, 7), fl = W(e, 8);
        if (bad || !d.label || !d.op || ne == 0 || (fl & ~1u) || W(e, 9) != 0 || (uint64_t)fe + ne > tb[E].count || (uint64_t)fz + nz > tb[Z].count ||
            (uint64_t)fb + nb > tb[B].count) {
            return r.fail("description record: file action %u has a bad string or a run outside its table", (unsigned)i);
        }
        d.ext = out->exts + fe;
        d.next = (int)ne;
        d.size_in = nz ? out->sizes + fz : 0;
        d.nsize_in = (int)nz;
        d.bind = nb ? out->binds + fb : 0;
        d.nbind = (int)nb;
        d.closes_menu = fl & 1;
    }
    // settings
    for (uint32_t i = 0; i < tb[S].count; i++) {
        const uint8_t *e = ENT(S, i);
        LibSettingDecl &d = out->settings[i];
        d.group = r.str(W(e, 0), &bad);
        d.key = r.str(W(e, 1), &bad);
        uint32_t fv = W(e, 2), nv = W(e, 3);
        d.lo = (int32_t)W(e, 4);
        d.hi = (int32_t)W(e, 5);
        d.def = (int32_t)W(e, 6);
        d.apply = r.str(W(e, 7), &bad);
        if (bad || !d.key || !d.key[0] || !d.apply || !d.apply[0] || (uint64_t)fv + nv > tb[V].count || (nv == 0 && d.hi < d.lo)) {
            return r.fail("description record: setting %u has a bad string, value run or range", (unsigned)i);
        }
        d.values = nv ? out->values + fv : 0;
        d.nvalues = (int)nv;
    }
    // the core's own menu (T104.2 D10)
    for (uint32_t i = 0; i < tb[K].count; i++) {
        const uint8_t *e = ENT(K, i);
        Task &d = out->tasks[i];
        memset(&d, 0, sizeof(d));
        d.label = r.str(W(e, 0), &bad);
        d.op = r.str(W(e, 1), &bad);
        uint32_t fb = W(e, 2), nb = W(e, 3), fl = W(e, 4);
        d.ask = r.str(W(e, 5), &bad);
        d.ask_state = r.str(W(e, 6), &bad);
        d.pick_label = r.str(W(e, 7), &bad);
        d.group = r.str(W(e, 8), &bad);
        const char hk = (char)(fl >> 8);                       // T228 R1: bits 15:8 the default key of the task's hotkey, 0 none
        if (bad || !d.label || !d.label[0] || !d.op || !d.op[0] || (fl & 0xFFFFu & ~0xFF03u) || (uint64_t)fb + nb > tb[B].count || (!d.ask && (d.ask_state || (fl & 2) || d.pick_label)) ||
            (d.pick_label && !(fl & 2)) || (hk && !((hk >= 'A' && hk <= 'Z') || (hk >= '0' && hk <= '9')))) {
            return r.fail("description record: task %u has a bad string, flag or binding run", (unsigned)i);
        }
        d.bind = nb ? out->binds + fb : 0;
        d.nbind = (int)nb;
        d.closes_menu = fl & 1;
        d.pick_dir = (fl & 2) != 0;
        d.hotkey = hk;
        if (fl >> 16) {                                        // the condition that lists the task (the offset + 1 of its text above the flags' low 16 bits)
            d.show_state = r.str((fl >> 16) - 1, &bad);
            if (bad) {
                return r.fail("description record: task %u has a bad condition", (unsigned)i);
            }
        }
    }
    out->ntasks = (int)tb[K].count;
    for (uint32_t i = 0; i < tb[M].count; i++) {
        const uint8_t *e = ENT(M, i);
        MediaSlot &d = out->media[i];
        memset(&d, 0, sizeof(d));
        d.name = r.str(W(e, 0), &bad);
        d.state = r.str(W(e, 1), &bad);
        d.eject_op = r.str(W(e, 2), &bad);
        uint32_t fb = W(e, 3), nb = W(e, 4);
        d.group = r.str(W(e, 5), &bad);
        if (bad || !d.name || !d.name[0] || !d.state || !d.state[0] || (uint64_t)fb + nb > tb[B].count || (!d.eject_op && nb)) {
            return r.fail("description record: media slot %u has a bad string or binding run", (unsigned)i);
        }
        d.eject_bind = nb ? out->binds + fb : 0;
        d.neject_bind = (int)nb;
        if (ver >= 4) {                                              // T212 D5 / D11: the drive list's data (media.mount, exts, empty_tasks, mounted_tasks, eject_closes)
            d.exts = r.str(W(e, 6), &bad);
            d.mount = r.str(W(e, 7), &bad);
            d.empty_tasks = r.str(W(e, 8), &bad);
            d.mounted_tasks = r.str(W(e, 9), &bad);
            uint32_t mf = W(e, 10);
            if (bad || (mf & ~1u)) {
                return r.fail("description record: media slot %u has a bad drive-list field", (unsigned)i);
            }
            d.eject_closes = mf & 1;
        }
    }
    out->nmedia = (int)tb[M].count;
    for (uint32_t i = 0; i < tb[X].count; i++) {
        const uint8_t *e = ENT(X, i);
        FileAction &d = out->facts[i];
        memset(&d, 0, sizeof(d));
        d.label = r.str(W(e, 0), &bad);
        d.op = r.str(W(e, 1), &bad);
        uint32_t fb = W(e, 2), nb = W(e, 3), fl = W(e, 4);
        d.ask = r.str(W(e, 5), &bad);
        int32_t off = (int32_t)W(e, 6);
        uint32_t fz = W(e, 7), nz = W(e, 8);
        if (bad || !d.label || !d.label[0] || !d.op || !d.op[0] || (fl & 0xFFFFu & ~0xFF07u) || (uint64_t)fb + nb > tb[B].count || (uint64_t)fz + nz > tb[Z].count || off < -32768 ||
            off > 32767 || (!d.ask && (off != 0 || (fl & 2))) || ((fl & 2) && off != 0)) {
            return r.fail("description record: file type action %u has a bad string, flag or run", (unsigned)i);
        }
        d.bind = nb ? out->binds + fb : 0;
        d.nbind = (int)nb;
        d.closes_menu = fl & 1;
        d.key = (char)((fl >> 8) & 0xFF);
        d.ask_off = (int16_t)off;
        if (fl & 2) {                                    // T228 R21: repeat - the ask word is the name of the operation that counts
            d.repeat_op = d.ask;
            d.ask = 0;
        }
        d.show = (fl & 4) != 0;
        d.size_in = nz ? out->sizes + fz : 0;
        d.nsize_in = (int)nz;
        if (fl >> 16) {                                        // the condition that offers the action (the offset + 1 of its text above the flags' low 16 bits)
            d.show_state = r.str((fl >> 16) - 1, &bad);
            if (bad) {
                return r.fail("description record: file type action %u has a bad condition", (unsigned)i);
            }
        }
    }
    for (uint32_t i = 0; i < tb[Y].count; i++) {
        const uint8_t *e = ENT(Y, i);
        FileType &d = out->ftypes[i];
        memset(&d, 0, sizeof(d));
        d.ext = r.str(W(e, 0), &bad);
        d.inner_ext = r.str(W(e, 1), &bad);
        uint32_t fa = W(e, 2), na = W(e, 3), fi = W(e, 4), ni = W(e, 5);
        if (bad || !d.ext || d.ext[0] != '.' || !d.ext[1] || na == 0 || (uint64_t)fa + na > tb[X].count || (uint64_t)fi + ni > tb[X].count || ((ni == 0) != (d.inner_ext == 0)) ||
            (d.inner_ext && d.inner_ext[0] != '.')) {
            return r.fail("description record: file type %u has a bad extension or a run outside the actions", (unsigned)i);
        }
        for (uint32_t j = 0; j < i; j++) {
            if (!strcmp(out->ftypes[j].ext, d.ext)) {
                return r.fail("description record: file type %s twice", d.ext);
            }
        }
        d.actions = out->facts + fa;
        d.nactions = (int)na;
        d.inner = ni ? out->facts + fi : 0;
        d.ninner = (int)ni;
    }
    out->nftypes = (int)tb[Y].count;
    for (uint32_t i = 0; i < tb[Q].count; i++) {
        const uint8_t *e = ENT(Q, i);
        Setting &d = out->csettings[i];
        memset(&d, 0, sizeof(d));
        d.key = r.str(W(e, 0), &bad);
        d.group = r.str(W(e, 1), &bad);
        uint32_t fv = W(e, 2), nv = W(e, 3), df = W(e, 4), when = W(e, 5);
        if (when >> 8) {                                       // the condition that lists the row (the offset + 1 of its text above the time code)
            d.show_state = r.str((when >> 8) - 1, &bad);
            when &= 0xFF;
        }
        if (when & 4) {                                        // T228 R21: the last value string is the state that locks the row (its text replaces the value); old records never set the flag
            if (nv >= 1 && (uint64_t)fv + nv <= tb[V].count) {
                d.lock_state = out->values[fv + nv - 1];
                nv--;
            }
            when &= 3;
        }
        d.apply = r.str(W(e, 6), &bad);
        d.base = (int32_t)W(e, 7);
        d.label = r.str(W(e, 8), &bad);                          // what the menu shows (null: the key); the key stays what the .cfg and REST name
        if (bad || !d.key || !d.key[0] || (d.apply ? !d.apply[0] : ver < 6) || nv < (when >= 2 ? 1u : 2u) || (uint64_t)fv + nv > tb[V].count || df >= nv || when > 3) {
            return r.fail("description record: core setting %u has a bad string, value run, default or time", (unsigned)i);
        }
        d.values = out->values + fv;
        d.nvalues = (int)nv;
        d.def = (int)df;
        d.when = (uint8_t)(when >= 2 ? 0 : when);
        d.kind = (uint8_t)(when >= 2 ? when - 1 : 0);      // 2: a text, 3: a secret text (its default is values[0], `base` its longest length)
        for (uint32_t j = 0; j < i; j++) {
            if (!strcasecmp(out->csettings[j].key, d.key)) {
                return r.fail("description record: core setting \"%s\" twice", d.key);
            }
        }
    }
    out->ncsettings = (int)tb[Q].count;
    if (ver >= 6) {                                            // T239.3 D17: the data sections, behind the string table to the record's end
        uint32_t soff = r.u32(DESCREC_HEADER_V5), ssz = r.u32(DESCREC_HEADER_V5 + 4);
        if (soff < r.str_off + r.str_size || (uint64_t)soff + ssz != n || ssz == 0) {
            return r.fail("description record: the data sections lie outside the record");
        }
        if (parse_sections(r, soff, ssz, out, tb[Q].count) != 0) {
            return -1;
        }
    }
    out->has_own = (flags & 2) != 0;
    if (out->has_own != (tb[K].count || tb[M].count || tb[Y].count || tb[Q].count)) {
        return r.fail("description record: the own-menu flag and the tables of the own menu disagree");
    }
    out->rec.struct_size = sizeof(LibRecord);
    out->rec.nodes = out->nodes;
    out->rec.nnodes = (int)tb[N].count;
    out->rec.file_actions = out->actions;
    out->rec.nfile_actions = (int)tb[F].count;
    out->rec.settings = out->settings;
    out->rec.nsettings = (int)tb[S].count;
    out->ov.hide = out->hide;
    out->ov.nhide = (int)tb[H].count;
    out->ov.rename = out->rename;
    out->ov.nrename = (int)tb[R].count;
    if (bad) {
        return r.fail("description record: a string lies outside the table");
    }
    return 0;
}

void descrec_apply(const DescRecord &rec, const CoreDesc *base, CoreDesc *out)
{
    *out = *base;
    if (rec.machine_info) {
        out->machine_info = rec.machine_info;
    }
    if (rec.has_pad) {
        out->pad = &rec.pad;                                   // T228 R10: the record's pad is the core's
    }
    if (rec.restart_clear) {
        out->cold_ram_bytes = rec.restart_clear;               // T228 R17: what "Restart core" clears, as the core's TOML declares it
        out->cold_ram_only = rec.restart_ram_only;
    }
    if (!rec.has_own) {
        return;
    }
    out->tasks = rec.ntasks ? rec.tasks : 0;
    out->ntasks = rec.ntasks;
    out->media = rec.nmedia ? rec.media : 0;
    out->nmedia = rec.nmedia;
    out->filetypes = rec.nftypes ? rec.ftypes : 0;
    out->nfiletypes = rec.nftypes;
    if (rec.ncsettings) {
        out->settings = rec.csettings;
        out->nsettings = rec.ncsettings;
        // T228 D10: the settings of `base` (the core's C++ ones, and the items the application added before the record came: Picture, Persist medium mounts, Home
        // Directory, Hotkeys, menubuild.cc) stand behind the record's own, so a core whose menu is a record has them as every other core does. The copy goes to
        // fixed places in the record's table, so a second call with the same base gives the same table. A record that has no settings of its own leaves the
        // base's as they are. When both do not fit into DR_CSETS the base's are dropped and the line says so.
        if (base->nsettings > 0 && base->settings != rec.csettings) {          // T239.3 D17: a descriptor made of this record (descrec_core) holds them already
            Setting *tab = const_cast<DescRecord &>(rec).csettings;
            int add = 0;
            for (int i = 0; i < base->nsettings; i++) {
                bool dup = false;                              // a record with data sections is the core's descriptor: the settings of it that `base` carries (the application's items were
                for (int j = 0; rec.has_core && j < rec.ncsettings && !dup; j++) {          // added behind them) are not listed twice
                    dup = !strcasecmp(base->settings[i].key, rec.csettings[j].key);
                }
                if (!dup) {
                    add++;
                }
            }
            if (rec.ncsettings + add <= DR_CSETS) {
                int n = rec.ncsettings;
                for (int i = 0; i < base->nsettings; i++) {
                    bool dup = false;
                    for (int j = 0; rec.has_core && j < rec.ncsettings && !dup; j++) {
                        dup = !strcasecmp(base->settings[i].key, rec.csettings[j].key);
                    }
                    if (!dup) {
                        tab[n++] = base->settings[i];
                    }
                }
                out->nsettings = n;
            } else {
                printf("[menu] description: %d settings and %d of the application's do not fit into %d; the application's are not listed\n", rec.ncsettings, add, DR_CSETS);
            }
        }
    }
}

bool descrec_merged(const DescRecord &rec, const char *name)
{
    if (!rec.merged || !name || !name[0]) {
        return false;
    }
    size_t l = strlen(name);
    for (const char *p = rec.merged; *p;) {
        const char *e = strchr(p, ',');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        if (n == l && !strncmp(p, name, n)) {
            return true;
        }
        if (!e) {
            break;
        }
        p = e + 1;
    }
    return false;
}

int descrec_core(DescRecord *rec, const CoreHooks *hooks, CoreDesc *out, char *why, int whylen)
{
    if (!rec->has_core) {
        snprintf(why, whylen, "description record: it has no data sections (no core section)");
        return -1;
    }
    memset(out, 0, sizeof(*out));
    memcpy(out->id, rec->ident.id, 4);
    out->cor_name = rec->ident.name;
    out->title = rec->ident.title;
    out->short_title = rec->ident.short_title;
    out->folder = rec->ident.folder;
    out->store = rec->ident.store;
    out->min_contract = rec->ident.min_contract;
    out->roms = rec->nroms ? rec->roms : 0;
    out->nroms = rec->nroms;
    out->settings = rec->ncsettings ? rec->csettings : 0;
    out->nsettings = rec->ncsettings;
    out->tasks = rec->ntasks ? rec->tasks : 0;
    out->ntasks = rec->ntasks;
    out->filetypes = rec->nftypes ? rec->ftypes : 0;
    out->nfiletypes = rec->nftypes;
    out->media = rec->nmedia ? rec->media : 0;
    out->nmedia = rec->nmedia;
    if (rec->has_video) {
        out->video = rec->video;
        out->freeze = rec->freeze;
    }
    out->reset_ms = (uint16_t)rec->ident.reset_ms;
    out->service_op = rec->ident.service_op;
    out->service_period_ms = (uint16_t)rec->ident.service_period_ms;
    const uint32_t fl = rec->ident.flags;
    out->poll_when_frozen = (fl & 1) != 0;
    out->board_caps = (fl & 2) != 0;
    out->id_shared = (fl & 4) != 0;
    out->own_volume = (fl & 8) != 0;
    out->no_tone = (fl & 16) != 0;
    out->menu_first = (fl & 32) != 0;
    out->needs_program = rec->ident.needs_program;
    out->program_ext = rec->ident.program_ext;
    out->type_op = rec->ident.type_op;
    out->run_op = rec->ident.run_op;
    out->core_id = rec->ident.core_id;
    out->live_ms = (uint16_t)rec->ident.live_ms;
    out->caps = rec->ncaps ? rec->caps : 0;
    out->ncaps = rec->ncaps;
    out->machine_info = rec->machine_info;
    out->pad = rec->has_pad ? &rec->pad : 0;
    out->cold_ram_bytes = rec->restart_clear;
    out->cold_ram_only = rec->restart_ram_only;
    out->hotkeys = rec->has_hot ? &rec->hot : 0;
    if (hooks) {
        out->init = hooks->init;
        out->release = hooks->release;
        out->warm_reset = hooks->warm_reset;
        out->cold_start = hooks->cold_start;
        out->media_flush = hooks->media_flush;
        out->media_pending = hooks->media_pending;
        out->register_ops = hooks->register_ops;
        out->rom_key = hooks->rom_key;
        out->mount_slots = hooks->mount_slots;
    }
    if (rec->nmount) {
        if (!hooks || !hooks->mount_slots) {
            snprintf(why, whylen, "description record, section mount slots: the core's code has no functions for the slots");
            return -1;
        }
        rec->mount.n = rec->nmount;
        rec->mount.names = rec->mount_names;
        rec->mount.spec = hooks->mount_slots->spec;
        rec->mount.mount = hooks->mount_slots->mount;
        rec->mount.eject = hooks->mount_slots->eject;
        out->mount_slots = &rec->mount;
    }
    return 0;
}
