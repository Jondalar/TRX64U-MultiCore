#include "txu.h"
#include "cfw.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

const char *txu_result_str(txu_result_t r)
{
    switch (r) {
    case TXU_OK: return "ok";
    case TXU_ERR_IO: return "the file cannot be read";
    case TXU_ERR_NOT_TXU: return "not a MultiCore update file";
    case TXU_ERR_FORMAT: return "the update file is damaged or incomplete";
    case TXU_ERR_CRC: return "the header of the update file does not match its checksum";
    case TXU_ERR_ORIGIN: return "the update file does not come from TRX";
    }
    return "?";
}

bool txu_is_name(const char *name)
{
    size_t l = name ? strlen(name) : 0;
    return l >= 4 && !strcasecmp(name + l - 4, ".txu");
}

char txu_running_version[32];

txu_result_t txu_probe(CorSource &src, TxuInfo *ci)
{
    memset(ci, 0, sizeof(*ci));
    ci->file_size = src.size();
    if (ci->file_size < 4) {
        return TXU_ERR_NOT_TXU;
    }
    uint8_t h[TXU_HDR_FIXED + 2 * TXU_PART_BYTES + 4];
    uint32_t n = ci->file_size < sizeof(h) ? ci->file_size : (uint32_t)sizeof(h);
    if (!src.read(0, h, n)) {
        return TXU_ERR_IO;
    }
    if (memcmp(h, "TXU1", 4)) {
        return TXU_ERR_NOT_TXU;
    }
    if (n < TXU_HDR_FIXED) {
        return TXU_ERR_FORMAT;
    }
    uint32_t hdr_len = le32(h + 4), flags = le32(h + 8), np = le32(h + 24);
    if (np < 1 || np > 2 || hdr_len != TXU_HDR_FIXED + TXU_PART_BYTES * np || n < hdr_len || flags != 0) {
        return TXU_ERR_FORMAT;
    }
    if (fw_crc32(h, hdr_len - 4) != le32(h + hdr_len - 4)) {
        return TXU_ERR_CRC;
    }
    memcpy(ci->origin, h + 28, 8);
    memcpy(ci->version, h + 36, 24);
    memcpy(ci->commit, h + 60, 16);
    ci->origin[8] = ci->version[24] = ci->commit[16] = 0;
    if (strcmp(ci->origin, "trx")) {
        return TXU_ERR_ORIGIN;
    }
    ci->host_key = le32(h + 12);
    ci->iface_provided = le32(h + 16);
    ci->iface_needed = le32(h + 20);
    ci->nparts = (int)np;
    uint32_t end = hdr_len;
    for (uint32_t i = 0; i < np; i++) {
        const uint8_t *p = h + (TXU_HDR_FIXED - 4) + i * TXU_PART_BYTES;
        TxuPart &t = ci->part[i];
        t.kind = le32(p);
        t.off = le32(p + 4);
        t.size = le32(p + 8);
        t.addr = le32(p + 12);
        memcpy(t.sha, p + 16, 32);
        uint32_t slot = t.kind == TXU_KIND_HOST ? FW_BIT_SLOT : FW_APP_SLOT;
        uint32_t addr = t.kind == TXU_KIND_HOST ? FW_BIT_ADDR : FW_APP_ADDR;
        if ((t.kind != TXU_KIND_HOST && t.kind != TXU_KIND_APP) || t.addr != addr || t.size == 0 || t.size > slot || (t.off & 3) || t.off < end ||
            t.off > ci->file_size || ci->file_size - t.off < t.size) {
            return TXU_ERR_FORMAT;
        }
        for (uint32_t k = 0; k < i; k++) {
            if (ci->part[k].kind == t.kind) {
                return TXU_ERR_FORMAT;                        // a kind twice
            }
        }
        end = t.off + t.size;
    }
    if (end != ci->file_size) {
        return TXU_ERR_FORMAT;                                // bytes behind the last part
    }
    const TxuPart *hp = ci->host(), *ap = ci->app();
    if (hp && (ci->host_key == 0 || ci->iface_provided == 0)) {
        return TXU_ERR_FORMAT;
    }
    if (!hp && (ci->host_key != 0 || ci->iface_provided != 0)) {
        return TXU_ERR_FORMAT;
    }
    if ((ap != 0) != (ci->iface_needed != 0)) {
        return TXU_ERR_FORMAT;
    }
    return TXU_OK;
}

void txu_check(const TxuInfo &ci, const TxuRunning &run, TxuCheck *o)
{
    memset(o, 0, sizeof(*o));
    o->has_host = ci.host() != 0;
    o->has_app = ci.app() != 0;
    o->host_key_changes = o->has_host && ci.host_key != run.host_key;
    uint32_t provided = o->has_host ? ci.iface_provided : run.iface_provided;
    uint32_t needed = o->has_app ? ci.iface_needed : run.iface_needed;
    if (provided != needed) {
        if (o->has_host && o->has_app) {
            snprintf(o->why, sizeof(o->why), "The app in this package needs host interface %u; the host in it provides %u.", (unsigned)needed, (unsigned)provided);
        } else if (o->has_app) {
            snprintf(o->why, sizeof(o->why), "The app in this package needs host interface %u; the running host provides %u.", (unsigned)needed, (unsigned)provided);
        } else {
            snprintf(o->why, sizeof(o->why), "The host in this package provides host interface %u; the app that stays needs %u.", (unsigned)provided,
                     (unsigned)needed);
        }
        return;
    }
    o->ok = true;
}

namespace {
struct MemSrc : CorSource {
    const uint8_t *p;
    uint32_t n;
    MemSrc(const uint8_t *p_, uint32_t n_) : p(p_), n(n_) { }
    uint32_t size() { return n; }
    bool read(uint32_t off, void *buf, uint32_t len)
    {
        if (off > n || n - off < len) {
            return false;
        }
        memcpy(buf, p + off, len);
        return true;
    }
};
}

// tools/trx_pkg.py bit_length_from_header: the length a Xilinx .bit file states in its header; 0 when it is not one. *data_off: where the
// configuration data starts behind the header.
static uint32_t bit_length_from_header(const uint8_t *b, uint32_t len, uint32_t *data_off)
{
    static const uint8_t magic[13] = { 0x00, 0x09, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x0F, 0xF0, 0x00, 0x00, 0x01 };
    if (len < 13 || memcmp(b, magic, 13)) {
        return 0;
    }
    uint32_t p = 13;
    static const char keys[4] = { 'a', 'b', 'c', 'd' };
    for (int i = 0; i < 4; i++) {
        if (p + 3 > len || b[p] != (uint8_t)keys[i]) {
            return 0;
        }
        p += 3 + (((uint32_t)b[p + 1] << 8) | b[p + 2]);
    }
    if (p + 5 > len || b[p] != 'e') {
        return 0;
    }
    *data_off = p + 5;
    return p + 5 + (((uint32_t)b[p + 1] << 24) | ((uint32_t)b[p + 2] << 16) | ((uint32_t)b[p + 3] << 8) | b[p + 4]);
}

static bool sha_equal(const char *hex, const uint8_t want[32])
{
    char w[65];
    sha256_hex(want, w);
    return !strcmp(hex, w);
}

static bool fail(TxuStaged *o, const char *why)
{
    o->ok = false;
    o->why = why;
    return false;
}

bool txu_stage(CorSource &src, const TxuInfo &ci, uint8_t *bit_dst, uint8_t *app_dst, FwStageProgress cb, void *ctx, bool (*abort)(void *), TxuStaged *o,
               FwImage *img)
{
    memset(o, 0, sizeof(*o));
    memset(img, 0, sizeof(*img));
    const TxuPart *hp = ci.host(), *ap = ci.app();
    if (!hp && !ap) {
        return fail(o, "the package holds no part");
    }
    uint32_t total = (hp ? hp->size : 0) + (ap ? ap->size : 0), done = 0;
    o->host_len = hp ? hp->size : 0;
    o->app_len = ap ? ap->size : 0;
    if (hp) {
        FwHash h;
        fw_hash_begin(&h, 0);
        if (!fw_read_part(src, hp->off, bit_dst, hp->size, hp->size, h, cb, ctx, &done, total, abort)) {
            return fail(o, abort && abort(ctx) ? "cancelled" : "the file cannot be read");
        }
        fw_hash_sha_hex(&h, o->host_sha);
        if (!sha_equal(o->host_sha, hp->sha)) {
            return fail(o, "the host part does not match its SHA-256");
        }
    }
    if (ap) {
        FwHash h;
        fw_hash_begin(&h, 0);
        if (!fw_read_part(src, ap->off, app_dst, ap->size, ap->size, h, cb, ctx, &done, total, abort)) {
            return fail(o, abort && abort(ctx) ? "cancelled" : "the file cannot be read");
        }
        fw_hash_sha_hex(&h, o->app_sha);
        if (!sha_equal(o->app_sha, ap->sha)) {
            return fail(o, "the app part does not match its SHA-256");
        }
    }
    uint32_t ax = 0;
    if (hp) {
        uint32_t data_off = 0;
        if (bit_length_from_header(bit_dst, hp->size, &data_off) != hp->size) {
            return fail(o, "the host part is not a bitstream file whose header length matches");
        }
        MemSrc ms(bit_dst, hp->size);
        uint32_t id = 0;
        if (!cfw_bit_idcode(ms, data_off, hp->size - data_off, &id) || ((id ^ CFW_IDCODE_XC7A50T) & CFW_IDCODE_MASK)) {
            return fail(o, "the host part is not a bitstream for this FPGA");
        }
        if (!fw_axss(bit_dst, hp->size, &ax) || ax != ci.host_key) {
            return fail(o, "the host part does not carry the host key its header names");
        }
        o->host_axss = ax;
    }
    const char *why = "";
    if (!fw_image_seal_parts(img, true, hp ? bit_dst : 0, hp ? hp->size : 0, ap ? app_dst : 0, ap ? ap->size : 0, ax, &why)) {
        return fail(o, why);
    }
    o->ok = true;
    o->why = "";
    return true;
}

void txu_survey_add(TxuSurvey *s, cor_result_t r, const CorInfo &ci, uint32_t running_key, uint32_t new_key)
{
    if (r == COR_ERR_LARGE) {
        s->large++;
        return;
    }
    if (r != COR_OK && r != COR_ERR_HOST) {
        return;                                               // not a layer-2 core this host could ever have run
    }
    s->partial++;
    bool works_now = r == COR_OK && (running_key == 0 || ci.host_key == running_key);
    bool works_after = new_key == 0 || ci.host_key == new_key;
    if (works_now && !works_after) {
        s->broken++;
    } else if (!works_now && works_after) {
        s->revived++;
    } else if (!works_now) {
        s->already_off++;
    }
}
