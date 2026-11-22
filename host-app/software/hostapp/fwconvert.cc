#include "fwconvert.h"
#include "sha256.h"
#include <string.h>
#include <stdio.h>
#include <strings.h>

const char *fwcv_text(FwConvResult r)
{
    switch (r) {
    case FWCV_OK: return "ok";
    case FWCV_EXISTS: return "a file of that name is there";
    case FWCV_ERR_NOROOM: return "no room for the .cor on that medium";
    case FWCV_ERR_READ: return "cannot read the firmware file";
    case FWCV_ERR_WRITE: return "cannot write the .cor";
    case FWCV_ERR_VERIFY: return "the .cor does not read back as written";
    case FWCV_ERR_FS: return "the file system answered with an error";
    case FWCV_CANCELLED: return "cancelled";
    }
    return "?";
}

FwCvTarget fwcv_target(const char *fw_path, char *out, int n)
{
    size_t l = strlen(fw_path);
    if (l < 5 || (strcasecmp(fw_path + l - 4, ".cfw") && strcasecmp(fw_path + l - 4, ".ue2")) || fw_path[l - 5] == '/' || fw_path[0] != '/') {
        return FWCT_NAME;                                // no extension of a firmware, or a file that is nothing but the extension
    }
    const char *med_end = strchr(fw_path + 1, '/');      // the medium is the first component; the file lies somewhere below it
    if (med_end == fw_path + 1 || !med_end || med_end >= fw_path + l - 4) {
        return FWCT_NAME;
    }
    size_t ml = (size_t)(med_end - fw_path - 1);
    if (ml == 5 && !strncasecmp(fw_path + 1, "flash", 5)) {
        return FWCT_FLASH;
    }
    const char *base = strrchr(fw_path, '/') + 1;
    size_t bl = (size_t)(fw_path + l - 4 - base);
    size_t need = 1 + ml + 7 + bl + 4 + 1;               // "/" medium "/cores/" name ".cor" NUL
    if ((int)need > n) {
        return FWCT_LONG;
    }
    memcpy(out, fw_path, 1 + ml);
    memcpy(out + 1 + ml, "/cores/", 7);
    memcpy(out + 1 + ml + 7, base, bl);
    memcpy(out + 1 + ml + 7 + bl, ".cor", 5);
    return FWCT_OK;
}

void fwcv_names(const CfwInfo &ci, const FwKnown *known, FwConvPlan *p)
{
    if (known) {
        snprintf(p->name, sizeof(p->name), "%s %s", known->vendor, known->name);
        snprintf(p->version, sizeof(p->version), "%s", known->version);
        return;
    }
    if (ci.self_name[0]) {                                  // T254: the name and version the firmware gives itself (cfw_read_self)
        snprintf(p->name, sizeof(p->name), "%s", ci.self_name);
        snprintf(p->version, sizeof(p->version), "%s", ci.self_ver);
        return;
    }
    snprintf(p->name, sizeof(p->name), "%s", ci.name);
    const char *sp = strchr(ci.name, ' ');                  // "<design> <date> <time>"
    if (sp && sp[1]) {
        char d[16];
        int i = 0;
        for (sp++; *sp && *sp != ' ' && i < 15; sp++) {
            d[i++] = *sp;
        }
        d[i] = 0;
        snprintf(p->version, sizeof(p->version), "bit %s", d);
    } else {
        snprintf(p->version, sizeof(p->version), "unknown");
    }
}

// ---- the manifest, as tools/cor_pack.py pack_large writes it: json.dumps(m, sort_keys=True, separators=(",", ":")) ---------------------
// The firmware's printf (seen in ue2emu, 2026-10-29) has no "%%", ignores a precision ("%.8s" prints the whole string) and prints %x in capitals, and the
// reader of a .cor takes hex in lower case only: the manifest is made without those (tests/hostapp/run_fw.sh looks for them in this file).
static void hex_lower(char *out, uint32_t v)
{
    static const char dig[] = "0123456789abcdef";
    for (int i = 0; i < 8; i++) {
        out[i] = dig[(v >> (28 - 4 * i)) & 15];
    }
    out[8] = 0;
}

static int jstr(char *o, int n, const char *s)
{
    int k = 0;
    if (n < 1) {
        return 0;
    }
    if (k < n) o[k++] = '"';
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            if (k + 2 < n) { o[k++] = '\\'; o[k++] = (char)c; }
        } else if (c < 0x20 || c >= 0x7F) {
            if (k + 6 < n) {                                       // json.dumps ensure_ascii; a .bit header is ASCII anyway
                static const char dig[] = "0123456789abcdef";
                o[k++] = '\\'; o[k++] = 'u'; o[k++] = '0'; o[k++] = '0'; o[k++] = dig[c >> 4]; o[k++] = dig[c & 15];
            }
        } else if (k < n) {
            o[k++] = (char)c;
        }
    }
    if (k < n) o[k++] = '"';
    o[k < n ? k : n - 1] = 0;
    return k;
}

static int manifest(char *o, int n, const FwConvPlan &p, const CfwInfo &ci, const char *bit_sha, const char *app_sha, const char *icon_sha, bool icon)
{
    int k = 0;
    char nm[160], vr[80];
    jstr(nm, sizeof(nm), p.name);
    jstr(vr, sizeof(vr), p.version);
    k += snprintf(o + k, n - k, "{\"app_entry\":%u,\"app_load\":%u,\"app_sha256\":\"%s\",\"app_size\":%u,\"bit_sha256\":\"%s\",\"bit_size\":%u,\"format\":2,\"fpga\":\"xc7a50t\",",
                  (unsigned)CFW_APP_ADDR, (unsigned)CFW_APP_ADDR, app_sha, (unsigned)ci.app_len, bit_sha, (unsigned)ci.bit_len);
    if (icon) {
        k += snprintf(o + k, n - k, "\"icon_sha256\":\"%s\",\"icon_size\":2048,", icon_sha);
    }
    char idc[9];
    hex_lower(idc, ci.idcode);
    k += snprintf(o + k, n - k, "\"id\":\"%s\",\"idcode\":\"%s\",\"kind\":\"large\",\"map\":\"%s\",\"name\":%s,\"origin\":\"foreign\",\"version\":%s}",
                  p.id, idc, ci.map == CFW_MAP_IDENTITY ? "identity" : "gideon_bl4", nm, vr);
    return k;
}

#define MANIFEST_MAX 2048
#define ICON_BYTES 2048

static uint64_t cor_size(int man_len, const CfwInfo &ci, bool icon)
{
    uint64_t body = 8 + (uint64_t)man_len + 8 + ci.app_len + (icon ? 8 + ICON_BYTES : 0) + 8 + ci.bit_len;
    return 8 + 4 + body;
}

FwConvResult fwcv_plan(FoFs &fs, const CfwInfo &ci, const FwKnown *known, bool has_icon, const char *dst, FwConvPlan *p)
{
    memset(p, 0, sizeof(*p));
    snprintf(p->dst, sizeof(p->dst), "%s", dst);
    fwcv_names(ci, known, p);
    snprintf(p->id, sizeof(p->id), "fw-00000000");
    char bs[65], as[65], is[65], m[MANIFEST_MAX + 256];
    memset(bs, 'a', 64); memset(as, 'a', 64); memset(is, 'a', 64);
    bs[64] = as[64] = is[64] = 0;
    int ml = manifest(m, sizeof(m), *p, ci, bs, as, is, has_icon);          // the length only: the hashes and the id have a fixed length
    if (ml <= 0 || ml > MANIFEST_MAX) {
        return FWCV_ERR_WRITE;
    }
    p->size = cor_size(ml, ci, has_icon);
    FoStat st;
    p->exists = fs.stat(dst, &st);
    if (p->exists && st.dir) {
        return FWCV_ERR_FS;
    }
    char dir[FO_PATH_MAX];                           // the folder that holds the file; the room is the room of its medium
    snprintf(dir, sizeof(dir), "%s", dst);
    char *sl = strrchr(dir, '/');
    if (sl && sl != dir) {
        *sl = 0;
    } else if (sl) {
        sl[1] = 0;
    }
    p->dir_missing = !fs.stat(dir, &st);
    if (!p->dir_missing && !st.dir) {
        return FWCV_ERR_FS;                          // a file where the folder should be
    }
    char vol[FO_PATH_MAX];
    snprintf(vol, sizeof(vol), "%s", dst);
    char *vs = strchr(vol + 1, '/');
    if (vs) {
        vs[1] = 0;
    }
    if (!fs.free_bytes(vol, &p->free)) {
        return FWCV_ERR_FS;
    }
    // the new file is written before the old one goes: room for both while it is written
    if (p->size + 4096 > p->free) {
        return FWCV_ERR_NOROOM;
    }
    return p->exists ? FWCV_EXISTS : FWCV_OK;
}

// ---- writing ---------------------------------------------------------------------------------------------------------------------------
struct Out {
    FoFs &fs;
    void *h;
    sha256_t sha;
    uint64_t done, total;
    FwConvProgress cb;
    void *ctx;
    bool ok;
    bool cancel;
    void put(const void *d, uint32_t n)
    {
        if (!ok) return;
        if (!fs.write(h, d, n)) { ok = false; return; }
        sha256_update(&sha, d, n);
        done += n;
        if (cb && cb(ctx, 1, done, total)) { cancel = true; ok = false; }
    }
};

static void u32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static bool copy_src(CorSource &src, uint32_t off, uint32_t len, Out &o, uint8_t *buf, uint32_t buflen)
{
    for (uint32_t d = 0; d < len && o.ok;) {
        uint32_t n = len - d < buflen ? len - d : buflen;
        if (!src.read(off + d, buf, n)) {
            return false;
        }
        o.put(buf, n);
        d += n;
    }
    return o.ok;
}

static bool hash_src(CorSource &src, uint32_t off, uint32_t len, char hex[65], uint8_t *buf, uint32_t buflen, FwConvProgress cb, void *ctx, uint64_t *done, uint64_t total, bool *cancel)
{
    sha256_t s;
    sha256_init(&s);
    for (uint32_t d = 0; d < len;) {
        uint32_t n = len - d < buflen ? len - d : buflen;
        if (!src.read(off + d, buf, n)) {
            return false;
        }
        sha256_update(&s, buf, n);
        d += n;
        *done += n;
        if (cb && cb(ctx, 0, *done, total)) {
            *cancel = true;
            return false;
        }
    }
    uint8_t dg[32];
    sha256_final(&s, dg);
    sha256_hex(dg, hex);
    return true;
}

static const char *dirname_of(const char *path, char *out, int n)
{
    snprintf(out, n, "%s", path);
    char *sl = strrchr(out, '/');
    if (sl) {
        *sl = 0;
    }
    return out;
}

FwConvResult fw_convert(FoFs &fs, CorSource &src, const CfwInfo &ci, const FwConvPlan &plan, const uint8_t *icon, bool replace, uint8_t *buf, uint32_t buflen,
                        FwConvProgress cb, void *ctx)
{
    if (plan.exists && !replace) {
        return FWCV_EXISTS;
    }
    char part[FO_PATH_MAX + 8], part_dir[FO_PATH_MAX];
    snprintf(part, sizeof(part), "%s.part", plan.dst);
    // pass 1: the hashes (the manifest comes first in the file)
    char bsha[65], asha[65], isha[65] = "";
    uint64_t done = 0, total = (uint64_t)ci.bit_len + ci.app_len;
    bool cancel = false;
    if (!hash_src(src, ci.bit_off, ci.bit_len, bsha, buf, buflen, cb, ctx, &done, total, &cancel) ||
        !hash_src(src, ci.app_data, ci.app_len, asha, buf, buflen, cb, ctx, &done, total, &cancel)) {
        return cancel ? FWCV_CANCELLED : FWCV_ERR_READ;
    }
    if (icon) {
        sha256_t s;
        uint8_t dg[32];
        sha256_init(&s);
        sha256_update(&s, icon, ICON_BYTES);
        sha256_final(&s, dg);
        sha256_hex(dg, isha);
    }
    FwConvPlan p = plan;
    memcpy(p.id, "fw-", 3);                                    // "fw-" and the first 8 characters of the bitstream's SHA-256 (no precision in the firmware's printf)
    memcpy(p.id + 3, bsha, 8);
    p.id[11] = 0;
    char man[MANIFEST_MAX + 256];
    int ml = manifest(man, sizeof(man), p, ci, bsha, asha, isha, icon != 0);
    if (ml <= 0 || ml > MANIFEST_MAX) {
        return FWCV_ERR_WRITE;
    }
    uint64_t size = cor_size(ml, ci, icon != 0);
    Out o = { fs, 0, {}, 0, size, cb, ctx, true, false };
    if (plan.dir_missing && !fs.make_dir(dirname_of(plan.dst, part_dir, sizeof(part_dir)))) {
        return FWCV_ERR_FS;
    }
    o.h = fs.open_write(part);
    if (!o.h) {
        return FWCV_ERR_FS;
    }
    sha256_init(&o.sha);
    uint8_t hd[12];
    memcpy(hd, "TRXCOR01", 8);
    o.put(hd, 8);
    u32(hd, (uint32_t)(size - 12));
    o.put(hd, 4);
    u32(hd, 1); u32(hd + 4, (uint32_t)ml);                          // record 1: the manifest
    o.put(hd, 8);
    o.put(man, (uint32_t)ml);
    u32(hd, 3); u32(hd + 4, ci.app_len);                            // record 3: the application, raw
    o.put(hd, 8);
    if (!copy_src(src, ci.app_data, ci.app_len, o, buf, buflen) && o.ok) {
        o.ok = false;
        fs.close(o.h);
        fs.remove(part);
        return FWCV_ERR_READ;
    }
    if (icon) {
        u32(hd, 4); u32(hd + 4, ICON_BYTES);                        // record 4: the icon
        o.put(hd, 8);
        o.put(icon, ICON_BYTES);
    }
    u32(hd, 5); u32(hd + 4, ci.bit_len);                            // record 5: the bitstream as it is
    o.put(hd, 8);
    if (!copy_src(src, ci.bit_off, ci.bit_len, o, buf, buflen) && o.ok) {
        o.ok = false;
        fs.close(o.h);
        fs.remove(part);
        return FWCV_ERR_READ;
    }
    bool closed = fs.close(o.h);
    if (!o.ok || !closed || o.done != size) {
        fs.remove(part);
        return o.cancel ? FWCV_CANCELLED : FWCV_ERR_WRITE;
    }
    uint8_t want[32];
    sha256_final(&o.sha, want);
    // read back
    void *r = fs.open_read(part);
    if (!r) {
        fs.remove(part);
        return FWCV_ERR_FS;
    }
    sha256_t s;
    sha256_init(&s);
    uint64_t rd = 0;
    for (;;) {
        int n = fs.read(r, buf, buflen);
        if (n < 0) {
            fs.close(r);
            fs.remove(part);
            return FWCV_ERR_VERIFY;
        }
        if (n == 0) {
            break;
        }
        sha256_update(&s, buf, (uint32_t)n);
        rd += (uint64_t)n;
        if (cb && cb(ctx, 2, rd, size)) {
            fs.close(r);
            fs.remove(part);
            return FWCV_CANCELLED;
        }
    }
    fs.close(r);
    uint8_t got[32];
    sha256_final(&s, got);
    if (rd != size || memcmp(got, want, 32)) {
        fs.remove(part);
        return FWCV_ERR_VERIFY;
    }
    if (plan.exists && !fs.remove(plan.dst)) {
        fs.remove(part);
        return FWCV_ERR_FS;
    }
    if (!fs.rename(part, plan.dst)) {
        fs.remove(part);
        return FWCV_ERR_FS;
    }
    return FWCV_OK;
}
