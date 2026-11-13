#include "cfw.h"
#include "handover.h"
#include <string.h>
#include <strings.h>
#include <stdio.h>

static void cut_copy(char *out, size_t outlen, const char *s, size_t n)
{
    size_t i = 0;
    while (i < n && i + 1 < outlen && s[i]) {
        out[i] = s[i];
        i++;
    }
    out[i] = 0;
}

const char *cfw_result_str(cfw_result_t r)
{
    switch (r) {
    case CFW_OK: return "ok";
    case CFW_ERR_IO: return "cannot read the firmware file";
    case CFW_ERR_FORMAT: return "not a firmware image (.cfw / .ue2)";
    case CFW_ERR_TRUNCATED: return "the firmware image is truncated";
    case CFW_ERR_NO_BIT: return "no bitstream in the firmware image";
    case CFW_ERR_NO_50T: return "no bitstream for this FPGA in the firmware image";
    case CFW_ERR_NO_APP: return "no application after the bitstreams in the firmware image";
    case CFW_ERR_LOAD: return "the application does not load and start at 0x30000";
    case CFW_ERR_RANGE: return "the firmware image is too large for the staging areas";
    case CFW_ERR_HASH: return "a part of the core package is damaged (SHA-256)";
    }
    return "?";
}

// T115 D9: a .cfw (Gideon's pre-header) or a .ue2 (the same image without it) is a foreign firmware
bool cfw_is_name(const char *name)
{
    size_t l = strlen(name);
    return l > 4 && (!strcasecmp(name + l - 4, ".cfw") || !strcasecmp(name + l - 4, ".ue2"));
}

static uint32_t be16(const uint8_t *p) { return ((uint32_t)p[0] << 8) | p[1]; }
static uint32_t be32(const uint8_t *p) { return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3]; }
static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static const uint8_t PRE_HEADER[10] = { 0xDE, 0xAD, 0x15, 0x41, 'U', '6', '4', 'M', 'K', '2' };
static const uint8_t BIT_MAGIC[13] = { 0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01 };

// ---- the .bit file: header fields a-d (NUL terminated strings), then 'e' and the 32-bit length of the configuration data
struct BitHdr {
    uint32_t hdr_len, data_len;
    char f[4][48];                  // a: design, b: part, c: date, d: time
};

static bool bit_header(CorSource &src, uint32_t pos, BitHdr &h)
{
    uint8_t b[512];
    uint32_t size = src.size();
    if (pos >= size) {
        return false;
    }
    uint32_t n = size - pos < sizeof(b) ? size - pos : (uint32_t)sizeof(b);
    if (n < 64 || !src.read(pos, b, n) || memcmp(b, BIT_MAGIC, sizeof(BIT_MAGIC))) {
        return false;
    }
    memset(&h, 0, sizeof(h));
    uint32_t p = sizeof(BIT_MAGIC);
    for (int i = 0; i < 4; i++) {
        if (p + 3 > n || b[p] != (uint8_t)('a' + i)) {
            return false;
        }
        uint32_t l = be16(b + p + 1);
        p += 3;
        if (l == 0 || p + l > n || b[p + l - 1] != 0) {
            return false;
        }
        strncpy(h.f[i], (const char *)b + p, sizeof(h.f[i]) - 1);
        p += l;
    }
    if (p + 5 > n || b[p] != 'e') {
        return false;
    }
    h.data_len = be32(b + p + 1);
    h.hdr_len = p + 5;
    return h.data_len >= 64;
}

// The first valid .bit header at or after `from`, below `limit`. The magic is also found inside the updater's code, so a
// hit counts only when the fields behind it make a header.
static cfw_result_t find_first_bit(CorSource &src, uint32_t from, uint32_t limit, uint32_t *pos)
{
    static uint8_t blk[4096];
    BitHdr h;
    uint32_t off = from;
    for (;;) {
        if (limit <= off) {
            break;
        }
        uint32_t n = limit - off > sizeof(blk) ? (uint32_t)sizeof(blk) : limit - off;
        if (n < sizeof(BIT_MAGIC)) {
            break;
        }
        if (!src.read(off, blk, n)) {
            return CFW_ERR_IO;
        }
        for (uint32_t i = 0; i + sizeof(BIT_MAGIC) <= n; i++) {
            if (blk[i] == 0x00 && blk[i + 1] == 0x09 && !memcmp(blk + i, BIT_MAGIC, sizeof(BIT_MAGIC)) && bit_header(src, off + i, h)) {
                *pos = off + i;
                return CFW_OK;
            }
        }
        if (off + n >= limit) {
            break;
        }
        off += n - (sizeof(BIT_MAGIC) - 1);
    }
    return CFW_ERR_NO_BIT;
}

// ---- the configuration packets inside a .bit: the IDCODE and whether the AXSS register is written
// (the same walk as cor_pack.py host_key_from_bit and the host boot ROM's flash_axss): after the sync word, a type-1
// header is followed by its words, a type-2 header by a word count; the walk ends at the DESYNC command.
#define SYNC_WORD 0xAA995566u
#define REG_CMD   0x04u
#define REG_IDCODE 0x0Cu
#define REG_AXSS  0x0Du
#define CMD_DESYNC 0x0Du

struct Win {
    CorSource &s;
    uint32_t base, len, at;
    uint32_t n;
    uint8_t b[512];
    Win(CorSource &src, uint32_t base_, uint32_t len_) : s(src), base(base_), len(len_), at(0xFFFFFFFFu), n(0) { }
    bool word(uint32_t rel, uint32_t *w)
    {
        if (rel > len || len - rel < 4) {
            return false;
        }
        uint32_t blk = rel & ~511u;
        if (blk != at) {
            n = len - blk < sizeof(b) ? len - blk : (uint32_t)sizeof(b);
            at = 0xFFFFFFFFu;
            if (!s.read(base + blk, b, n)) {
                return false;
            }
            at = blk;
        }
        uint32_t o = rel - blk;
        if (o + 4 > n) {
            return false;
        }
        *w = be32(b + o);
        return true;
    }
};

// False when there is no sync word in the first 1 KiB. stop_at_id: return once the IDCODE is known.
static bool scan_config(CorSource &src, uint32_t off, uint32_t len, bool stop_at_id, uint32_t *idcode, bool *axss)
{
    *idcode = 0;
    *axss = false;
    Win w(src, off, len);
    uint32_t v, i = 0;
    for (;; i += 4) {
        if (i >= 1024 || !w.word(i, &v)) {
            return false;
        }
        if (v == SYNC_WORD) {
            break;
        }
    }
    i += 4;
    while (w.word(i, &v)) {
        if (*axss && *idcode) {
            return true;                            // both answers are in: a compressed stream is not walked to its end
        }
        i += 4;
        uint32_t t = v >> 29;
        if (t == 1) {
            uint32_t op = (v >> 27) & 3, reg = (v >> 13) & 0x3FFF, cnt = v & 0x7FF;
            if (op == 2) {
                uint32_t val = 0;
                if (cnt == 1 && !w.word(i, &val)) {
                    return true;
                }
                if (reg == REG_AXSS) {
                    *axss = true;
                } else if (reg == REG_IDCODE && cnt == 1) {
                    *idcode = val;
                    if (stop_at_id) {
                        return true;
                    }
                } else if (reg == REG_CMD && cnt == 1 && val == CMD_DESYNC) {
                    return true;
                }
            }
            i += 4 * cnt;
        } else if (t == 2) {
            i += 4 * (v & 0x7FFFFFFu);
        } else {
            break;
        }
    }
    return true;
}

bool cfw_bit_idcode(CorSource &src, uint32_t off, uint32_t len, uint32_t *idcode)
{
    bool axss;
    return scan_config(src, off, len, true, idcode, &axss) && *idcode != 0;
}

// ---- the application record: {load, length, start}, three little-endian words
static bool app_plausible(const uint8_t *r)
{
    uint32_t load = le32(r), len = le32(r + 4), start = le32(r + 8);
    return len >= 0x10000 && len <= 0x08000000u && !(load & 3) && start >= load && (uint64_t)start < (uint64_t)load + len;
}

// ---- a large core packaged as a .cor (T115 section 7): cor.cc reads the container and the manifest; here the bitstream is looked at
static cfw_result_t probe_large_cor(CorSource &src, uint32_t running_idcode, CfwInfo &ci)
{
    static CorInfo c;
    cor_result_t r = cor_probe(src, c);
    if (r == COR_ERR_IO) {
        return CFW_ERR_IO;
    }
    if (r != COR_ERR_LARGE) {
        return CFW_ERR_FORMAT;                                      // a layer-2 .cor, or not a package
    }
    if ((c.idcode ^ running_idcode) & CFW_IDCODE_MASK) {
        return CFW_ERR_NO_50T;
    }
    if (c.size[COR_PART_FULL] > CFW_BIT_MAX || c.size[COR_PART_APP] > CFW_APP_MAX) {
        return CFW_ERR_RANGE;
    }
    // the record is the .bit file; without a header (raw configuration data) the whole record is the data
    BitHdr h;
    uint32_t doff = c.off[COR_PART_FULL], dlen = c.size[COR_PART_FULL];
    if (bit_header(src, doff, h)) {
        if ((uint64_t)h.hdr_len + h.data_len > dlen) {
            return CFW_ERR_TRUNCATED;
        }
        doff += h.hdr_len;
        dlen = h.data_len;
    }
    uint32_t id = 0;
    bool ax = false;
    if (!scan_config(src, doff, dlen, false, &id, &ax) || !id) {
        return CFW_ERR_FORMAT;
    }
    if ((id ^ running_idcode) & CFW_IDCODE_MASK) {
        return CFW_ERR_NO_50T;                                      // the manifest says 50T, the bitstream is another part
    }
    if ((id ^ c.idcode) & CFW_IDCODE_MASK) {
        return CFW_ERR_FORMAT;
    }
    if (!strcmp(c.map, "identity") != ax) {
        return CFW_ERR_FORMAT;                                      // the map is decided by the AXSS write, the manifest only repeats it
    }
    memset(&ci, 0, sizeof(ci));
    ci.file_size = c.file_size;
    ci.from_cor = true;
    ci.idcode = id;
    ci.map = ax ? CFW_MAP_IDENTITY : CFW_MAP_GIDEON_BL4;
    ci.bit_off = c.off[COR_PART_FULL];
    ci.bit_len = c.size[COR_PART_FULL];
    ci.app_off = ci.app_data = c.off[COR_PART_APP];
    ci.app_len = c.size[COR_PART_APP];
    ci.app_load = c.app_load;
    ci.app_entry = c.app_entry;
    memcpy(ci.bit_sha, c.sha[COR_PART_FULL], sizeof(ci.bit_sha));
    memcpy(ci.app_sha, c.sha[COR_PART_APP], sizeof(ci.app_sha));
    snprintf(ci.name, sizeof(ci.name), "%s", c.name);
    snprintf(ci.part, sizeof(ci.part), "%s", c.fpga);
    snprintf(ci.id, sizeof(ci.id), "%s", c.id);
    return CFW_OK;
}

cfw_result_t cfw_probe(CorSource &src, uint32_t running_idcode, CfwInfo &ci)
{
    memset(&ci, 0, sizeof(ci));
    uint32_t size = src.size();
    ci.file_size = size;
    uint8_t magic[8];
    if (size >= 20 && src.read(0, magic, 8) && !memcmp(magic, "TRXCOR01", 8)) {
        return probe_large_cor(src, running_idcode, ci);
    }
    uint8_t pre[sizeof(PRE_HEADER)];
    if (size < 0x1000 || !src.read(0, pre, sizeof(pre))) {
        return size < 0x1000 ? CFW_ERR_FORMAT : CFW_ERR_IO;
    }
    // T115 D9: a .ue2 has no pre-header; it starts with the updater's record {load, length, load}. The real files (Commodore 1.1.0, Gideon 3.14, 3.15-dev,
    // read 2026-10-29) load the updater at 0x03000000, the record in a .cfw after its pre-header does the same; 0x30000 is what the benches made of it.
    uint32_t from = sizeof(PRE_HEADER);
    if (memcmp(pre, PRE_HEADER, sizeof(PRE_HEADER))) {
        uint8_t r0[12];
        if (!src.read(0, r0, 12) || !app_plausible(r0) || le32(r0) != le32(r0 + 8) || (le32(r0) != CFW_APP_ADDR && le32(r0) != CFW_UPDATER_ADDR)) {
            return CFW_ERR_FORMAT;
        }
        from = 12;
    }
    uint32_t pos = 0;
    cfw_result_t r = find_first_bit(src, from, size < CFW_SCAN_MAX ? size : CFW_SCAN_MAX, &pos);
    if (r != CFW_OK) {
        return r;
    }

    // Every bitstream in turn (the U64-II image carries one per FPGA size); the first one for this FPGA is the choice,
    // the application record is the one after the LAST of them. Gaps between the pieces are alignment, at most 16 bytes.
    bool have = false, have_app = false, ax = false;
    BitHdr best;
    memset(&best, 0, sizeof(best));
    uint32_t best_off = 0, best_len = 0, best_id = 0, last_end = 0, app_at = 0;
    uint8_t rec[12];
    for (int nbits = 0; nbits < 8; nbits++) {
        BitHdr h;
        if (!bit_header(src, pos, h)) {
            return CFW_ERR_FORMAT;
        }
        uint64_t total = (uint64_t)h.hdr_len + h.data_len;
        if ((uint64_t)pos + total > size) {
            return CFW_ERR_TRUNCATED;
        }
        uint32_t id = 0;
        bool a = false;
        bool got = scan_config(src, pos + h.hdr_len, h.data_len, false, &id, &a);
        if (!have && got && id && !((id ^ running_idcode) & CFW_IDCODE_MASK)) {
            have = true;
            best = h;
            best_off = pos;
            best_len = (uint32_t)total;
            best_id = id;
            ax = a;
        }
        uint32_t end = pos + (uint32_t)total;
        last_end = end;
        // What follows within 16 bytes (alignment): the record with load and start 0x30000, else the next .bit, else a record that
        // loads somewhere else (refused below). A zero word in front of the record shifts a window onto {0, load, length}, which
        // is why a window with load 0 is never taken for a record.
        bool exact = false, next_bit = false, wrong = false;
        uint32_t exact_at = 0, bit_at = 0, wrong_at = 0;
        for (uint32_t at = (end + 3) & ~3u; at <= end + 16 && !exact; at += 4) {
            if (at + 12 > size || !src.read(at, rec, 12)) {
                continue;
            }
            BitHdr h2;
            if (app_plausible(rec) && le32(rec) == CFW_APP_ADDR && le32(rec + 8) == CFW_APP_ADDR) {
                exact = true;
                exact_at = at;
            } else if (!next_bit && bit_header(src, at, h2)) {
                next_bit = true;
                bit_at = at;
            } else if (!wrong && app_plausible(rec) && le32(rec) != 0) {
                wrong = true;
                wrong_at = at;
            }
        }
        if (exact || (!next_bit && wrong)) {
            have_app = true;
            app_at = exact ? exact_at : wrong_at;
            break;
        }
        if (!next_bit) {
            break;
        }
        pos = bit_at;
    }
    if (!have) {
        return CFW_ERR_NO_50T;
    }
    if (!have_app) {
        return last_end + 12 > size ? CFW_ERR_TRUNCATED : CFW_ERR_NO_APP;
    }
    if (!src.read(app_at, rec, 12)) {
        return CFW_ERR_IO;
    }
    ci.app_off = app_at;
    ci.app_data = app_at + 12;
    ci.app_load = le32(rec);
    ci.app_len = le32(rec + 4);
    ci.app_entry = le32(rec + 8);
    if (ci.app_load != CFW_APP_ADDR || ci.app_entry != CFW_APP_ADDR) {
        return CFW_ERR_LOAD;
    }
    if (ci.app_len > CFW_APP_MAX || best_len > CFW_BIT_MAX) {
        return CFW_ERR_RANGE;
    }
    if ((uint64_t)app_at + 12 + ci.app_len > size) {
        return CFW_ERR_TRUNCATED;
    }
    ci.idcode = best_id;
    ci.bit_off = best_off;
    ci.bit_len = best_len;
    ci.map = ax ? CFW_MAP_IDENTITY : CFW_MAP_GIDEON_BL4;
    char design[24];
    strncpy(design, best.f[0], sizeof(design) - 1);
    design[sizeof(design) - 1] = 0;
    char *semi = strchr(design, ';');
    if (semi) {
        *semi = 0;
    }
    char d20[21], v10[11], v8[9];   // the board's printf ignores a precision: cut by hand
    cut_copy(d20, sizeof(d20), design, 20);
    cut_copy(v10, sizeof(v10), best.f[2], 10);
    cut_copy(v8, sizeof(v8), best.f[3], 8);
    snprintf(ci.name, sizeof(ci.name), "%s %s %s", d20, v10, v8);
    strncpy(ci.part, best.f[1], sizeof(ci.part) - 1);
    return CFW_OK;
}

// ---- the DDR2 address map (D3). Measured on the board 2026-10-21 (T115 section 3.3): the full firmware's controller
// moves 8-byte units, the host's 16-byte units, and only CPU address bits 6:3 differ: ours[3] = theirs[6],
// ours[6:4] = theirs[5:3]. Every other bit is the same.
uint32_t cfw_map_off(int kind, uint32_t off)
{
    if (kind == CFW_MAP_IDENTITY) {
        return off;
    }
    return (off & ~0x78u) | (((off >> 6) & 1) << 3) | (((off >> 3) & 7) << 4);
}

uint32_t cfw_unmap_off(int kind, uint32_t off)
{
    if (kind == CFW_MAP_IDENTITY) {
        return off;
    }
    return (off & ~0x78u) | (((off >> 3) & 1) << 6) | (((off >> 4) & 7) << 3);
}

typedef uint32_t __attribute__((may_alias)) cfw_u32;

void cfw_stage_copy(int kind, void *dst, uint32_t off, const void *src, uint32_t n)
{
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (uint32_t i = 0; i < n; i += 8) {
        cfw_u32 *dp = (cfw_u32 *)(d + cfw_map_off(kind, off + i));
        const cfw_u32 *sp = (const cfw_u32 *)(s + i);
        dp[0] = sp[0];
        dp[1] = sp[1];
    }
}

void cfw_iprog_seq(uint32_t faddr, uint8_t out[CFW_IPROG_LEN])
{
    // dummy word, sync, NOP, WBSTAR := faddr, CMD := IPROG, NOP, NOP: the words in bitstream (file) order
    const uint8_t seq[CFW_IPROG_LEN] = {
        0xFF, 0xFF, 0xFF, 0xFF, 0xAA, 0x99, 0x55, 0x66, 0x20, 0x00, 0x00, 0x00,
        0x30, 0x02, 0x00, 0x01, (uint8_t)(faddr >> 24), (uint8_t)(faddr >> 16), (uint8_t)(faddr >> 8), (uint8_t)faddr,
        0x30, 0x00, 0x80, 0x01, 0x00, 0x00, 0x00, 0x0F,
        0x20, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
    };
    memcpy(out, seq, CFW_IPROG_LEN);
}

void cfw_handover_page(const CfwInfo &ci, const char *path, uint8_t *page, const int *ymdhms)
{
    static_assert(sizeof(Handover) <= CFW_HANDOVER_BYTES, "the hand-over fits its page");
    uint32_t plain[CFW_HANDOVER_BYTES / 4];
    memset(plain, 0, sizeof(plain));
    if (ci.from_cor) {
        // two descriptors share the ID AMIG (A500, A1200): the application picks by the core's id, and by the manifest's name when there is
        // none. Without the name the A1200 stayed held, "no bundle to say which" (board 2026-10-26); the display name of the manifest
        // need not equal the descriptor's, the id does (T115 7, H2)
        handover_fill((Handover *)plain, path, ci.name, "", 0, 0, ci.id);
        if (ymdhms) {
            handover_set_time((Handover *)plain, ymdhms[0], ymdhms[1], ymdhms[2], ymdhms[3], ymdhms[4], ymdhms[5]);
        }
    }
    cfw_stage_copy(ci.map, page, 0, plain, CFW_HANDOVER_BYTES);
}
