// The Neo Geo ROM sets (T143): see neoset.h for what goes where and why.
#include "neoset.h"
#include "neogeo_regs.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static uint32_t smear(uint32_t v)
{
    v |= v >> 1; v |= v >> 2; v |= v >> 4; v |= v >> 8; v |= v >> 16;
    return v;
}

void neo_wr32(CoreIo *io, uint32_t reg, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        io->wr(reg + (uint32_t)i, (uint8_t)(v >> (8 * i)));
    }
}

uint32_t neo_rd32(CoreIo *io, uint32_t reg)
{
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) {
        v |= (uint32_t)io->rd(reg + (uint32_t)i) << (8 * i);
    }
    return v;
}

// ---- romsets.xml -------------------------------------------------------------------------------------------------------------
// A pull scanner for the tags of an XML file: the text between '<' and '>' (attribute values may hold '>'), comments, declarations and processing instructions skipped.
class XmlTags {
public:
    explicit XmlTags(CoreFile *f) : f_(f), size_(f->size()), pos_(0), n_(0), i_(0) { }
    bool next(char *tag, int cap)
    {
        for (;;) {
            int c;
            do { c = get(); } while (c >= 0 && c != '<');
            if (c < 0) {
                return false;
            }
            c = get();
            if (c == '!' || c == '?') {
                if (c == '!') {
                    int c2 = get(), c3 = get();
                    if (c2 == '-' && c3 == '-') {              // a comment: up to "-->"
                        int a = 0, b = 0, d = get();
                        while (d >= 0 && !(a == '-' && b == '-' && d == '>')) {
                            a = b; b = d; d = get();
                        }
                        continue;
                    }
                    c = c3;
                }
                while (c >= 0 && c != '>') {
                    c = get();
                }
                continue;
            }
            int n = 0;
            int quote = 0;
            while (c >= 0) {
                if (quote) {
                    if (c == quote) {
                        quote = 0;
                    }
                } else if (c == '"' || c == '\'') {
                    quote = c;
                } else if (c == '>') {
                    break;
                }
                if (n < cap - 1) {
                    tag[n++] = (char)c;
                }
                c = get();
            }
            tag[n] = 0;
            return c >= 0;
        }
    }
private:
    int get()
    {
        if (i_ >= n_) {
            if (pos_ >= size_) {
                return -1;
            }
            uint32_t want = size_ - pos_ > sizeof(buf_) ? (uint32_t)sizeof(buf_) : (uint32_t)(size_ - pos_);
            if (!f_->read(pos_, buf_, want)) {
                return -1;
            }
            pos_ += want; n_ = (int)want; i_ = 0;
        }
        return buf_[i_++];
    }
    CoreFile *f_;
    uint64_t size_, pos_;
    uint8_t buf_[1024];
    int n_, i_;
};

static bool xml_attr(const char *tag, const char *name, char *out, int cap)
{
    size_t nl = strlen(name);
    for (const char *p = tag; *p; p++) {
        if ((p == tag || isspace((unsigned char)p[-1])) && !strncmp(p, name, nl) && p[nl] == '=' && (p[nl + 1] == '"' || p[nl + 1] == '\'')) {
            char q = p[nl + 1];
            const char *v = p + nl + 2;
            int n = 0;
            while (*v && *v != q) {
                if (n < cap - 1) {
                    out[n++] = *v;
                }
                v++;
            }
            out[n] = 0;
            return true;
        }
    }
    return false;
}

static uint32_t num(const char *s)
{
    return (uint32_t)strtoul(s, 0, 0);
}

static bool names_has(const char *csv, const char *set)
{
    char one[NEO_NAME];
    const char *p = csv;
    while (*p) {
        while (*p == ' ' || *p == ',') {
            p++;
        }
        int n = 0;
        while (*p && *p != ',') {
            if (n < NEO_NAME - 1) {
                one[n++] = *p;
            }
            p++;
        }
        while (n && one[n - 1] == ' ') {
            n--;
        }
        one[n] = 0;
        if (n && !strcasecmp(one, set)) {
            return true;
        }
    }
    return false;
}

static bool tag_is(const char *tag, const char *word)
{
    size_t n = strlen(word);
    return !strncmp(tag, word, n) && (tag[n] == 0 || isspace((unsigned char)tag[n]) || tag[n] == '/');
}

bool neo_xml_find(CoreFs *fs, const char *xml_path, const char *set, NeoSetInfo *info)
{
    memset(info, 0, sizeof(*info));
    snprintf(info->name, sizeof(info->name), "%s", set);
    info->rom_wait = -1;
    info->p_wait = -1;
    CoreFile *f = fs->open(xml_path, false);
    if (!f) {
        return false;
    }
    XmlTags x(f);
    static char tag[700];
    char v[160];
    bool in = false;
    while (x.next(tag, sizeof(tag))) {
        if (!in) {
            if (tag_is(tag, "romset") && xml_attr(tag, "name", v, sizeof(v)) && names_has(v, set)) {
                info->found = true;
                if (xml_attr(tag, "pcm", v, sizeof(v))) info->pcm = (uint8_t)num(v);
                if (xml_attr(tag, "ct0", v, sizeof(v))) info->ct0 = (uint8_t)num(v);
                if (xml_attr(tag, "link", v, sizeof(v))) info->link = (uint8_t)num(v);
                if (xml_attr(tag, "cmc", v, sizeof(v))) info->cmc = (uint8_t)num(v);
                if (xml_attr(tag, "sma", v, sizeof(v))) info->sma = (uint8_t)num(v);
                if (xml_attr(tag, "pvc", v, sizeof(v))) info->pvc = (uint8_t)num(v);
                if (xml_attr(tag, "xram", v, sizeof(v))) info->xram = (uint8_t)num(v);
                if (xml_attr(tag, "ms5p", v, sizeof(v))) info->ms5p = (uint8_t)num(v);
                if (xml_attr(tag, "rom_wait", v, sizeof(v))) info->rom_wait = (int8_t)num(v);
                if (xml_attr(tag, "p_wait", v, sizeof(v))) info->p_wait = (int8_t)num(v);
                size_t L = strlen(tag);
                if (L && tag[L - 1] == '/') {
                    break;                                       // an entry without lines: <romset .../>
                }
                in = true;
            }
        } else if (tag_is(tag, "file")) {
            if (info->nfiles < NEO_FILES_MAX && xml_attr(tag, "name", v, sizeof(v))) {
                NeoFileRef &r = info->f[info->nfiles];
                snprintf(r.name, sizeof(r.name), "%s", v);
                r.index = xml_attr(tag, "index", v, sizeof(v)) ? (int)num(v) : -1;
                r.offset = xml_attr(tag, "offset", v, sizeof(v)) ? num(v) : 0;
                r.size = xml_attr(tag, "size", v, sizeof(v)) ? num(v) : (xml_attr(tag, "length", v, sizeof(v)) ? num(v) : 0);
                info->nfiles++;
            }
        } else if (tag[0] == '/' && tag_is(tag + 1, "romset")) {
            break;
        }
    }
    fs->close(f);
    return true;
}

uint32_t neo_cfg_word(const NeoSetInfo &s, bool adpcma_ext)
{
    uint32_t pchip = s.pvc ? 2u : (s.sma ? (uint32_t)s.sma + 2u : 0u);
    uint32_t chip = s.ct0 ? 1u : (s.link ? 2u : 0u);
    uint32_t rw = s.rom_wait < 0 ? 1u : (uint32_t)(s.rom_wait ? 1 : 0);     // the entry does not say: one wait state (the slow, safe choice)
    uint32_t pw = s.p_wait < 0 ? 2u : (uint32_t)(s.p_wait & 3);
    return (s.ms5p ? NEO_CFG_MS5P : 0) | (s.xram ? NEO_CFG_XRAM : 0) | (adpcma_ext ? NEO_CFG_ADPCMA_EXT : 0) | (pchip << 20) | (s.pcm ? NEO_CFG_PCM : 0) |
           (chip << 24) | ((uint32_t)(s.cmc & 3) << 26) | (rw << 28) | (pw << 29);
}

// ---- the parts -----------------------------------------------------------------------------------------------------------------
static int kind_of(int index)
{
    if (index == 4 || index == 5 || index == 6) return NK_P;
    if (index == 8) return NK_S;
    if (index == 9) return NK_M;
    if (index == 15) return NK_CRAW;
    if (index >= 16 && index < 64) return NK_V;
    if (index >= 64 && index < 256) return NK_C;
    return 0;
}

int neo_parts_xml(CoreFs *fs, const char *dir, const NeoSetInfo &s, NeoParts *out, char *msg, int msglen)
{
    memset(out, 0, sizeof(*out));
    out->info = s;
    if (!s.found) {
        snprintf(msg, msglen, "%s: romsets.xml has no entry of this name", s.name);
        return 1;
    }
    if (s.nfiles == 0) {
        snprintf(msg, msglen, "%s: the entry of romsets.xml lists no files (the Darksoft style); the names the MiSTer loader gives them are not known here - use a .neo file or an entry with files", s.name);
        return 1;
    }
    int missing = 0;
    char first[NEO_NAME] = "";
    for (int i = 0; i < s.nfiles; i++) {
        const NeoFileRef &r = s.f[i];
        int k = kind_of(r.index);
        if (!k) {
            snprintf(msg, msglen, "%s: %s has the index %d, which the loader does not know", s.name, r.name, r.index);
            return 1;
        }
        char path[NEO_PATH];
        snprintf(path, sizeof(path), "%s/%s", dir, r.name);
        uint64_t size;
        bool isdir;
        if (!fs->stat(path, &size, &isdir) || isdir) {
            if (!missing) {
                snprintf(first, sizeof(first), "%s", r.name);
            }
            missing++;
            continue;
        }
        NeoPart &p = out->p[out->n++];
        p.kind = (uint8_t)k;
        p.index = r.index;
        p.src_off = r.offset;
        uint32_t fsz = fs_size32(size);
        uint32_t avail = fsz > r.offset ? fsz - r.offset : 0;
        p.size = r.size && r.size < avail ? r.size : avail;
        snprintf(p.path, sizeof(p.path), "%s", path);
    }
    if (missing) {
        snprintf(msg, msglen, "%s: %s is missing in %s%s", s.name, first, dir, missing > 1 ? " (and more)" : "");
        return 1;
    }
    return 0;
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int neo_parts_neo(CoreFs *fs, const char *path, const NeoSetInfo &s, NeoParts *out, char *msg, int msglen)
{
    memset(out, 0, sizeof(*out));
    out->info = s;
    CoreFile *f = fs->open(path, false);
    if (!f) {
        snprintf(msg, msglen, "cannot open %s", path);
        return 1;
    }
    uint8_t h[64];
    uint64_t fsize = f->size();
    bool ok = fsize >= 4096 && f->read(0, h, sizeof(h));
    if (!ok || h[0] != 'N' || h[1] != 'E' || h[2] != 'O' || h[3] != 1) {
        fs->close(f);
        snprintf(msg, msglen, "%s is not a .neo file (no NEO header)", path);
        return 1;
    }
    uint32_t sz[6];                                              // P, S, M, V1, V2, C
    uint64_t total = 4096;
    for (int i = 0; i < 6; i++) {
        sz[i] = le32(h + 4 + 4 * i);
        total += sz[i];
    }
    if (total > fsize) {
        fs->close(f);
        snprintf(msg, msglen, "%s: the header says %u bytes, the file has %u", path, (unsigned)total, (unsigned)fs_size32(fsize));
        return 1;
    }
    // the 68k's byte order of the P ROM: "NEO-GEO" at $100 reads in order when the file is in the 68k's order (it has to be swapped for the core)
    uint8_t sig[8] = { 0 };
    if (sz[0] >= 0x108 && f->read(4096 + 0x100, sig, 8)) {
        out->p_swap = !memcmp(sig, "NEO-GEO", 7);
    }
    fs->close(f);
    uint32_t off = 4096;
    auto add = [&](int kind, int index, uint32_t so, uint32_t n) {
        if (n && out->n < NEO_FILES_MAX) {
            NeoPart &p = out->p[out->n++];
            p.kind = (uint8_t)kind; p.index = index; p.src_off = so; p.size = n;
            snprintf(p.path, sizeof(p.path), "%s", path);
        }
    };
    if (sz[0] <= 0x100000) {
        add(NK_P, 4, off, sz[0]);
    } else {
        add(NK_P, 4, off, 0x100000);
        add(NK_P, 6, off + 0x100000, sz[0] - 0x100000);
    }
    off += sz[0];
    add(NK_S, 8, off, sz[1]); off += sz[1];
    add(NK_M, 9, off, sz[2]); off += sz[2];
    add(NK_V, 16, off, sz[3]); off += sz[3];
    add(NK_V, 48, off, sz[4]); off += sz[4];
    add(NK_CNEO, 64, off, sz[5]);
    return 0;
}

// ---- layout and masks ------------------------------------------------------------------------------------------------------------
static uint32_t p_base(int index)
{
    return index == 4 ? 0x200000u : index == 5 ? 0x280000u : 0x300000u;
}

int neo_plan(const NeoParts &p, uint32_t rom_base, uint32_t m1_base, NeoPlan *out, char *msg, int msglen)
{
    memset(out, 0, sizeof(*out));
    uint32_t pend = 0x300000, p2hi = 0, cor = 0, cspan = 0, v1end = 0, v2end = 0, mmax = 0;
    bool vabove = false;
    for (int i = 0; i < p.n; i++) {
        const NeoPart &q = p.p[i];
        if (!q.size) {
            continue;
        }
        switch (q.kind) {
        case NK_P: {
            uint32_t b = p_base(q.index);
            uint32_t n = q.index == 5 && q.size > 0x80000 ? 0x80000 : q.size;
            if (b + n > pend) pend = b + n;
            if (b + n > 0x300000) {                      // the P2 part (the bank switched megabytes from $300000): its largest offset
                uint32_t hi = b + n - 1 - 0x300000;
                if (hi > p2hi) p2hi = hi;
            }
            break;
        }
        case NK_S:
            if (q.size > 0x80000) {
                snprintf(msg, msglen, "the S ROM is %u bytes, the core takes 512 KB at most", (unsigned)q.size);
                return 1;
            }
            break;
        case NK_M:
            if (q.size - 1 > mmax) mmax = q.size - 1;
            break;
        case NK_V: {
            uint32_t vv = (uint32_t)(q.index - 16) * 0x80000u;
            uint64_t end = (uint64_t)vv + q.size;
            if (vv < 0x1000000u) {
                uint32_t e1 = end > 0x1000000u ? 0x1000000u : (uint32_t)end;
                if (e1 > v1end) v1end = e1;
            }
            if (end > 0x1000000u) {
                uint32_t e2 = (uint32_t)(end - 0x1000000u);
                if (e2 > v2end) v2end = e2;
                vabove = true;
            }
            break;
        }
        case NK_C: {
            uint32_t pair = (uint32_t)(q.index - 64) >> 1, parity = (uint32_t)q.index & 1, w = (q.size + 1) / 2;
            uint32_t rel_end = (pair << 20) + (w - 1) * 4 + 2 * parity + 2;
            if (rel_end > cspan) cspan = rel_end;
            cor |= (pair << 20) | (smear(w - 1) << 2) | (parity << 1);
            break;
        }
        case NK_CNEO:
        case NK_CRAW:
            if (q.size > cspan) cspan = q.size;
            cor |= smear(q.size - 1);
            break;
        }
    }
    out->c_start = (uint8_t)((pend + 0xFFFFF) >> 20);
    if (out->c_start < 3) out->c_start = 3;
    out->p2_mask = p2hi ? smear(p2hi) : 0;
    out->c_mask = cor ? smear(cor) : 0;
    out->m_mask = smear(mmax);
    out->adpcma_ext = p.info.pcm == 2 || (p.info.pcm && vabove);
    out->v1_mask = out->adpcma_ext ? 0x1FFFFFFu : (v1end ? smear(v1end - 1) : 0);
    out->v2_mask = out->adpcma_ext ? 0 : (v2end ? smear(v2end - 1) : 0);
    out->rom_base = rom_base;
    uint32_t vend = rom_base + ((uint32_t)out->c_start << 20) + cspan;
    out->v1_base = (vend + 15) & ~15u;
    out->v2_base = out->adpcma_ext ? out->v1_base + 0x1000000u : ((out->v1_base + v1end + 15) & ~15u);
    out->m1_base = m1_base;
    out->end = out->v2_base + v2end;
    if (out->end < out->v1_base + v1end) out->end = out->v1_base + v1end;
    out->cfg = neo_cfg_word(p.info, out->adpcma_ext);
    if (out->end > NEO_DDR_END || out->end < vend) {
        snprintf(msg, msglen, "%s does not fit the DDR2: %u MB of ROM space, V ROM up to %u MB, the DDR2 has %u MB from the ROM base", p.info.name,
                 (unsigned)((vend - rom_base) >> 20), (unsigned)((v1end + v2end) >> 20), (unsigned)((NEO_DDR_END - rom_base) >> 20));
        return 1;
    }
    return 0;
}

// ---- writing -----------------------------------------------------------------------------------------------------------------------
static bool copy_run(CoreCtx *c, uint32_t src, uint32_t dst, uint32_t len, int mask_mode)
{
    CoreIo *io = c->io;
    uint8_t before = io->rd(NEO_MV_DONE);
    neo_wr32(io, NEO_CP_SRC, src);
    neo_wr32(io, NEO_CP_DST, dst);
    neo_wr32(io, NEO_CP_LEN, len);
    io->wr(NEO_MV_CMD, (uint8_t)(NEO_MV_COPY | (mask_mode & 3)));
    io->wr(NEO_MV_GO, 1);
    uint32_t t0 = io->now_ms();
    while (io->rd(NEO_MV_DONE) == before) {
        if (io->now_ms() - t0 > 2000 + len / 8192) {
            return false;
        }
        io->delay_ms(1);
    }
    return true;
}

bool neo_put(CoreCtx *c, uint32_t abs_dst, const uint8_t *data, uint32_t n, int mask_mode)
{
    if (mask_mode == 0 && abs_dst >= NEO_DDR_BASE && abs_dst + n <= NEO_CPU_LIMIT) {
        c->io->ddr_write(abs_dst - NEO_DDR_BASE, data, n);
        return true;
    }
    uint32_t done = 0;
    while (done < n) {
        uint32_t m = n - done > NEO_STAGE_SIZE ? NEO_STAGE_SIZE : n - done;
        c->io->ddr_write(NEO_STAGE_ABS - NEO_DDR_BASE, data + done, m);
        uint32_t m16 = (m + 15u) & ~15u;
        if (m16 != m) {
            static const uint8_t zero[16] = { 0 };
            c->io->ddr_write(NEO_STAGE_ABS - NEO_DDR_BASE + m, zero, m16 - m);
        }
        if (!copy_run(c, NEO_STAGE_ABS, abs_dst + done, m16, mask_mode)) {
            return false;
        }
        done += m;
    }
    return true;
}

void neo_fix_rearrange(uint8_t *data, uint32_t n)
{
    static const uint8_t blk[4] = { 2, 3, 0, 1 };
    for (uint32_t t = 0; t + 32 <= n; t += 32) {
        uint8_t in[32];
        memcpy(in, data + t, 32);
        for (int line = 0; line < 8; line++) {
            for (int k = 0; k < 4; k++) {
                data[t + (uint32_t)line * 4 + (uint32_t)k] = in[blk[k] * 8 + line];
            }
        }
    }
}

static uint32_t v_abs(const NeoPlan &pl, uint32_t vv)
{
    return vv < 0x1000000u ? pl.v1_base + vv : pl.v2_base + (vv - 0x1000000u);
}

#define NEO_CHUNK 32768
static uint8_t g_in[NEO_CHUNK];
static uint8_t g_out[2 * NEO_CHUNK];

static int fail(char *msg, int msglen, const char *what, const char *path)
{
    snprintf(msg, msglen, "%s %s", what, path);
    return 1;
}

int neo_load(CoreCtx *c, const NeoParts &p, const NeoPlan &pl, char *msg, int msglen)
{
    for (int i = 0; i < p.n; i++) {
        const NeoPart &q = p.p[i];
        if (!q.size) {
            continue;
        }
        CoreFile *f = c->fs->open(q.path, false);
        if (!f) {
            return fail(msg, msglen, "cannot open", q.path);
        }
        for (uint32_t off = 0; off < q.size; off += NEO_CHUNK) {
            uint32_t n = q.size - off > NEO_CHUNK ? NEO_CHUNK : q.size - off;
            if (!f->read((uint64_t)q.src_off + off, g_in, n)) {
                c->fs->close(f);
                return fail(msg, msglen, "cannot read (the medium is gone?)", q.path);
            }
            bool ok = true;
            switch (q.kind) {
            case NK_P: {
                if (q.index == 5 && off >= 0x80000) {
                    break;
                }
                if (p.p_swap) {
                    for (uint32_t k = 0; k + 1 < n; k += 2) {
                        uint8_t t = g_in[k]; g_in[k] = g_in[k + 1]; g_in[k + 1] = t;
                    }
                }
                ok = neo_put(c, pl.rom_base + p_base(q.index) + off, g_in, n, 0);
                break;
            }
            case NK_S: {
                if (off + n > 0x80000) {
                    n = off >= 0x80000 ? 0 : 0x80000 - off;
                }
                uint32_t n32 = (n + 31u) & ~31u;
                memset(g_in + n, 0, n32 - n);
                neo_fix_rearrange(g_in, n32);
                if (n32) ok = neo_put(c, pl.rom_base + 0x80000 + off, g_in, n32, 0);
                break;
            }
            case NK_M:
                ok = neo_put(c, pl.m1_base + off, g_in, n, 0);
                break;
            case NK_V: {
                uint32_t vv = (uint32_t)(q.index - 16) * 0x80000u + off;
                if (vv < 0x1000000u && vv + n > 0x1000000u) {          // the chunk crosses from the first V space into the second
                    uint32_t a = 0x1000000u - vv;
                    ok = neo_put(c, v_abs(pl, vv), g_in, a, 0) && neo_put(c, v_abs(pl, 0x1000000u), g_in + a, n - a, 0);
                } else {
                    ok = neo_put(c, v_abs(pl, vv), g_in, n, 0);
                }
                break;
            }
            case NK_C: {
                // MiSTer's load_crom_to_mem: the file's 16 bit words are permuted inside every block of 32 words (spr_convert_skp: out[i] = in[(i & ~31) | ((i >> 1) & 15) |
                // (((i & 1) ^ 1) << 4)]) and spread to every second word of a region twice the size; c1 (even index) fills the odd words (bytes 2,3 of every four), c2 the even ones.
                uint32_t n64 = (n + 63u) & ~63u, slot = ((uint32_t)q.index & 1) ^ 1, pair = (uint32_t)(q.index - 64) >> 1;
                memset(g_in + n, 0, n64 - n);
                memset(g_out, 0, n64 * 2);
                for (uint32_t b = 0; b < n64; b += 64) {
                    for (uint32_t i = 0; i < 32; i++) {
                        uint32_t src = ((i >> 1) & 15) | (((i & 1) ^ 1) << 4);
                        const uint8_t *w = g_in + b + 2 * src;
                        uint8_t *d = g_out + 2 * b + 4 * i + 2 * slot;
                        d[0] = w[0];
                        d[1] = w[1];
                    }
                }
                ok = neo_put(c, pl.rom_base + ((uint32_t)pl.c_start << 20) + (pair << 20) + 2 * off, g_out, n64 * 2, slot ? 2 : 1);
                break;
            }
            case NK_CNEO:                                                // .neo: C1 and C2 bytes alternate; MiSTer swaps the middle two bytes of every four (spr_bswap) and permutes with spr_convert_dbl
            case NK_CRAW: {                                              // a C file of index 15 (the whole sprite ROM in one file): spr_convert_dbl without the swap
                uint32_t n128 = (n + 127u) & ~127u;
                memset(g_in + n, 0, n128 - n);
                if (q.kind == NK_CNEO) {
                    for (uint32_t k = 0; k + 4 <= n128; k += 4) {
                        uint8_t t = g_in[k + 1]; g_in[k + 1] = g_in[k + 2]; g_in[k + 2] = t;
                    }
                }
                for (uint32_t b = 0; b < n128; b += 128) {              // spr_convert_dbl: out[i] = in[(i & ~63) | ((i ^ 1) & 1) | ((i >> 1) & 0x1E) | (((i & 2) ^ 2) << 4)]
                    for (uint32_t i = 0; i < 64; i++) {
                        uint32_t src = ((i ^ 1) & 1) | ((i >> 1) & 0x1E) | (((i & 2) ^ 2) << 4);
                        g_out[b + 2 * i] = g_in[b + 2 * src];
                        g_out[b + 2 * i + 1] = g_in[b + 2 * src + 1];
                    }
                }
                ok = neo_put(c, pl.rom_base + ((uint32_t)pl.c_start << 20) + off, g_out, n128, 0);
                break;
            }
            default:
                break;
            }
            if (!ok) {
                c->fs->close(f);
                snprintf(msg, msglen, "the DDR2 copy engine did not answer while loading %s", q.path);
                return 1;
            }
        }
        c->fs->close(f);
    }
    CoreIo *io = c->io;
    neo_wr32(io, NEO_ROM_BASE, pl.rom_base);
    neo_wr32(io, NEO_V1_BASE, pl.v1_base);
    neo_wr32(io, NEO_V2_BASE, pl.v2_base);
    neo_wr32(io, NEO_M1_BASE, pl.m1_base);
    neo_wr32(io, NEO_P2_MASK, pl.p2_mask);
    neo_wr32(io, NEO_C_MASK, pl.c_mask);
    neo_wr32(io, NEO_V1_MASK, pl.v1_mask);
    neo_wr32(io, NEO_V2_MASK, pl.v2_mask);
    neo_wr32(io, NEO_M_MASK, pl.m_mask);
    io->wr(NEO_C_START, pl.c_start);
    neo_wr32(io, NEO_CFG, pl.cfg);
    return 0;
}
