// The .cor core bundle, read side. See cor.h.
#include "cor.h"
#include "sha256.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define CHUNK (64 * 1024)

const char *cor_result_str(cor_result_t r)
{
    switch (r) {
    case COR_OK: return "ok";
    case COR_ERR_IO: return "cannot read the file";
    case COR_ERR_FORMAT: return "not a valid core bundle";
    case COR_ERR_CONTRACT: return "built for another host contract";
    case COR_ERR_HASH: return "SHA-256 mismatch: the file is damaged";
    case COR_ERR_RANGE: return "application outside the allowed memory";
    case COR_ERR_HOST: return "built for another host";
    case COR_ERR_LARGE: return "a large core: it replaces the host and starts as full firmware";
    }
    return "?";
}

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }

bool cor_json_get(const char *json, const char *key, char *out, int outlen)
{
    char pat[40];
    int kl = strlen(key);
    if (kl + 4 > (int)sizeof(pat)) {
        return false;
    }
    pat[0] = '"';
    memcpy(pat + 1, key, kl);
    pat[kl + 1] = '"';
    pat[kl + 2] = ':';
    pat[kl + 3] = 0;
    const char *p = strstr(json, pat);
    if (!p) {
        return false;
    }
    p += kl + 3;
    int n = 0;
    if (*p == '"') {
        p++;
        while (*p && *p != '"') {
            char c = *p++;
            if (c == '\\') {
                c = *p++;
                if (c == 'u') {
                    p += 4;
                    c = '?';
                } else if (!c) {
                    return false;
                }
            }
            if (n + 1 >= outlen) {
                return false;
            }
            out[n++] = c;
        }
        if (*p != '"') {
            return false;
        }
    } else {
        while (*p && *p != ',' && *p != '}') {
            if (n + 1 >= outlen) {
                return false;
            }
            out[n++] = *p++;
        }
    }
    out[n] = 0;
    return true;
}

static bool get_u32(const char *json, const char *key, uint32_t *v)
{
    char b[16];
    if (!cor_json_get(json, key, b, sizeof(b)) || !b[0]) {
        return false;
    }
    char *e;
    unsigned long x = strtoul(b, &e, 10);
    if (*e) {
        return false;
    }
    *v = (uint32_t)x;
    return true;
}

// An 8 hex digit manifest field (the host key, T84 D5b) as a number.
static bool get_hex32(const char *json, const char *key, uint32_t *v)
{
    char b[16];
    if (!cor_json_get(json, key, b, sizeof(b)) || strlen(b) != 8) {
        return false;
    }
    uint32_t x = 0;
    for (int i = 0; i < 8; i++) {
        char c = b[i];
        if (c >= '0' && c <= '9') {
            x = (x << 4) | (uint32_t)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            x = (x << 4) | (uint32_t)(c - 'a' + 10);
        } else {
            return false;
        }
    }
    *v = x;
    return true;
}

static bool get_hex(const char *json, const char *key, char *out)
{
    char b[80];
    if (!cor_json_get(json, key, b, sizeof(b)) || strlen(b) != 64) {
        return false;
    }
    for (int i = 0; i < 64; i++) {
        char c = b[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) {
            return false;
        }
    }
    memcpy(out, b, 65);
    return true;
}

cor_result_t cor_probe(CorSource &src, CorInfo &ci)
{
    memset(&ci, 0, sizeof(ci));
    uint32_t fsz = src.size();
    ci.file_size = fsz;
    uint8_t h[20];
    if (fsz < 20 || !src.read(0, h, 20)) {
        return fsz < 20 ? COR_ERR_FORMAT : COR_ERR_IO;
    }
    if (memcmp(h, "TRXCOR01", 8) || le32(h + 8) != fsz - 12) {
        return COR_ERR_FORMAT;
    }
    if (le32(h + 12) != 1 || le32(h + 16) > COR_MANIFEST_MAX || 20 + le32(h + 16) > fsz) {
        return COR_ERR_FORMAT;
    }
    uint32_t mlen = le32(h + 16);
    char m[COR_MANIFEST_MAX + 1];
    if (!src.read(20, m, mlen)) {
        return COR_ERR_IO;
    }
    m[mlen] = 0;
    uint32_t dg = 2166136261u;                       // FNV-1a over the 20 header bytes and the manifest (the SHA values of every part are in it)
    for (uint32_t i = 0; i < 20; i++) {
        dg = (dg ^ h[i]) * 16777619u;
    }
    for (uint32_t i = 0; i < mlen; i++) {
        dg = (dg ^ (uint8_t)m[i]) * 16777619u;
    }
    ci.hdr_digest = dg ? dg : 1;

    // record layout after the manifest: types strictly increasing, each inside the file
    uint32_t p = 20 + mlen, last = 1;
    while (p < fsz) {
        uint8_t rh[8];
        if (p + 8 > fsz) {
            return COR_ERR_FORMAT;
        }
        if (!src.read(p, rh, 8)) {
            return COR_ERR_IO;
        }
        uint32_t t = le32(rh), n = le32(rh + 4);
        p += 8;
        if (t <= last || t > 7 || n > fsz - p) {
            return COR_ERR_FORMAT;
        }
        last = t;
        ci.off[t - 2] = p;
        ci.size[t - 2] = n;
        p += n;
    }
    char kind[16];
    ci.large = cor_json_get(m, "kind", kind, sizeof(kind)) && !strcmp(kind, "large");
    if (ci.large ? (ci.size[COR_PART_PARTIAL] || !ci.size[COR_PART_FULL] || !ci.size[COR_PART_APP])
                 : (!ci.size[COR_PART_PARTIAL] || !ci.size[COR_PART_APP] || ci.size[COR_PART_FULL])) {
        return COR_ERR_FORMAT;                                       // record 5 belongs to a large core only, record 2 to the other kind
    }

    static const char *pk[COR_PARTS] = { "partial", "app", "icon", "bit", "desc", "libs" };
    for (int i = 0; i < COR_PARTS; i++) {
        char k[24];
        uint32_t sz;
        strcpy(k, pk[i]);
        strcat(k, "_size");
        if (!ci.size[i]) {
            if (get_u32(m, k, &sz)) {
                return COR_ERR_FORMAT;
            }
            continue;
        }
        if (!get_u32(m, k, &sz) || sz != ci.size[i]) {
            return COR_ERR_FORMAT;
        }
        strcpy(k, pk[i]);
        strcat(k, "_sha256");
        if (!get_hex(m, k, ci.sha[i])) {
            return COR_ERR_FORMAT;
        }
    }
    if (ci.size[COR_PART_DESC] || ci.size[COR_PART_LIBS]) {
        uint32_t rf;                                                 // T104.1: records 6 and 7 are announced by the manifest ("records": 1)
        if (!get_u32(m, "records", &rf) || rf != 1) {
            return COR_ERR_FORMAT;
        }
    }
    if (ci.large) {
        uint32_t fmt;
        char b[16];
        if (!get_u32(m, "format", &fmt) || fmt != 2 ||
            !cor_json_get(m, "name", ci.name, sizeof(ci.name)) ||
            !cor_json_get(m, "version", ci.version, sizeof(ci.version)) ||
            !cor_json_get(m, "fpga", ci.fpga, sizeof(ci.fpga)) ||
            !cor_json_get(m, "id", ci.id, sizeof(ci.id)) ||
            !cor_json_get(m, "map", ci.map, sizeof(ci.map)) || (strcmp(ci.map, "identity") && strcmp(ci.map, "gideon_bl4")) ||
            !get_hex32(m, "idcode", &ci.idcode) ||
            !get_u32(m, "app_load", &ci.app_load) || !get_u32(m, "app_entry", &ci.app_entry) ||
            cor_json_get(m, "contract", b, sizeof(b)) || cor_json_get(m, "host_key", b, sizeof(b))) {
            return COR_ERR_FORMAT;                                   // a large core has neither contract nor host key
        }
        if (ci.app_load != ci.app_entry || ci.app_entry != 0x30000u) {
            return COR_ERR_FORMAT;
        }
        if (ci.size[COR_PART_ICON] && ci.size[COR_PART_ICON] != 2048) {
            return COR_ERR_FORMAT;
        }
        // T137 D1/D2: who made it. No key = ours; the three flash keys belong to a backup, and it needs all of them at the one layout there is.
        char org[16];
        bool has_org = cor_json_get(m, "origin", org, sizeof(org));
        bool has_fl = cor_json_get(m, "flash_bit", b, sizeof(b)) || cor_json_get(m, "flash_app", b, sizeof(b)) || cor_json_get(m, "flash_id", b, sizeof(b));
        ci.origin = COR_ORIGIN_TRX;
        if (has_org && !strcmp(org, "foreign")) {
            ci.origin = COR_ORIGIN_FOREIGN;
        } else if (has_org && !strcmp(org, "backup")) {
            ci.origin = COR_ORIGIN_BACKUP;
        } else if (has_org) {
            return COR_ERR_FORMAT;
        }
        if (ci.origin == COR_ORIGIN_BACKUP) {
            if (!get_u32(m, "flash_bit", &ci.flash_bit) || !get_u32(m, "flash_app", &ci.flash_app) ||
                !cor_json_get(m, "flash_id", ci.flash_id, sizeof(ci.flash_id)) || !ci.flash_id[0] ||
                ci.flash_bit != 0x000000u || ci.flash_app != 0x220000u) {
                return COR_ERR_FORMAT;
            }
        } else if (has_fl) {
            return COR_ERR_FORMAT;
        }
        return COR_ERR_LARGE;
    }
    if (!cor_json_get(m, "name", ci.name, sizeof(ci.name)) ||
        !cor_json_get(m, "version", ci.version, sizeof(ci.version)) ||
        !cor_json_get(m, "fpga", ci.fpga, sizeof(ci.fpga)) ||
        !get_u32(m, "contract", &ci.contract) || !get_hex32(m, "host_key", &ci.host_key) ||
        !get_u32(m, "app_load", &ci.app_load) ||
        !get_u32(m, "app_entry", &ci.app_entry)) {
        return COR_ERR_FORMAT;
    }
    if (ci.app_entry < ci.app_load || ci.app_entry - ci.app_load >= ci.size[COR_PART_APP] ||
        (ci.size[COR_PART_PARTIAL] & 3)) {
        return COR_ERR_FORMAT;
    }
    if (ci.size[COR_PART_ICON] && ci.size[COR_PART_ICON] != 2048) {
        return COR_ERR_FORMAT;
    }
    if (ci.contract != COR_HOST_CONTRACT) {
        return COR_ERR_CONTRACT;
    }
    // T84 D5b: the partial fits ONE host. The contract only rises when the partition's edge
    // changes, so a host re-routed with the same ports keeps it and an old partial would hang in
    // the ICAP with a black picture instead of being named.
    if (cor_host_key && ci.host_key != cor_host_key) {
        return COR_ERR_HOST;
    }
    return COR_OK;
}

uint32_t cor_host_key;
uint32_t (*cor_clock)(void);
bool cor_abort;
uint32_t cor_t_read, cor_t_hash;

cor_result_t cor_read_part(CorSource &src, const CorInfo &ci, int part, uint8_t *dst,
                           cor_progress_t progress, void *ctx, bool verify)
{
    sha256_t c;
    sha256_init(&c);
    uint32_t total = ci.size[part];
    for (uint32_t done = 0; done < total;) {
        uint32_t n = (total - done < CHUNK) ? total - done : CHUNK;
        uint32_t t0 = cor_clock ? cor_clock() : 0;
        if (!src.read(ci.off[part] + done, dst + done, n)) {
            return COR_ERR_IO;
        }
        uint32_t t1 = cor_clock ? cor_clock() : 0;
        if (verify) {
            sha256_update(&c, dst + done, n);
        }
        if (cor_clock) {
            cor_t_read += t1 - t0;
            cor_t_hash += cor_clock() - t1;
        }
        done += n;
        if (progress) {
            progress(done, total, ctx);
        }
        if (cor_abort) {
            return COR_ERR_IO;
        }
    }
    if (!verify) {
        return COR_OK;
    }
    uint8_t d[32];
    char hx[65];
    sha256_final(&c, d);
    sha256_hex(d, hx);
    return memcmp(hx, ci.sha[part], 64) ? COR_ERR_HASH : COR_OK;
}

// SHA-256 of one part, streamed through a static buffer (the part itself is not kept).
static cor_result_t hash_part(CorSource &src, const CorInfo &ci, int part, cor_progress_t progress, void *ctx)
{
    static uint8_t buf[CHUNK];
    sha256_t c;
    sha256_init(&c);
    uint32_t total = ci.size[part];
    for (uint32_t done = 0; done < total;) {
        uint32_t n = (total - done < CHUNK) ? total - done : CHUNK;
        if (!src.read(ci.off[part] + done, buf, n)) {
            return COR_ERR_IO;
        }
        sha256_update(&c, buf, n);
        done += n;
        if (progress) {
            progress(done, total, ctx);
        }
    }
    uint8_t d[32];
    char hx[65];
    sha256_final(&c, d);
    sha256_hex(d, hx);
    return memcmp(hx, ci.sha[part], 64) ? COR_ERR_HASH : COR_OK;
}

cor_result_t cor_verify_part(CorSource &src, const CorInfo &ci, int part, cor_progress_t progress, void *ctx)
{
    return part >= 0 && part < COR_PARTS && ci.size[part] ? hash_part(src, ci, part, progress, ctx) : COR_ERR_FORMAT;
}

bool cor_large_verify(CorSource &src, const CorInfo &ci, const char **why, cor_progress_t progress, void *ctx)
{
    static const int parts[5] = { COR_PART_APP, COR_PART_ICON, COR_PART_FULL, COR_PART_DESC, COR_PART_LIBS };
    static const char *const names[5] = { "application", "icon", "bitstream", "description", "libraries" };
    for (int i = 0; i < 5; i++) {
        if (!ci.size[parts[i]]) {
            continue;
        }
        cor_result_t r = hash_part(src, ci, parts[i], progress, ctx);
        if (r != COR_OK) {
            static char msg[80];
            snprintf(msg, sizeof(msg), "%s: %s", names[i], cor_result_str(r));
            *why = msg;
            return false;
        }
    }
    return true;
}
