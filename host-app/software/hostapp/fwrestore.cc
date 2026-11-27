#include "fwrestore.h"
#include "ourfw.h"
#include <string.h>
#include <stdio.h>

static uint32_t pad4(uint32_t n) { return (n + 3u) & ~3u; }

const char *fw_kind_name(FwKind k)
{
    switch (k) {
    case FWK_NONE: return "not a firmware";
    case FWK_FIRMWARE: return "firmware file";
    case FWK_BACKUP_COR: return "backup of this board";
    case FWK_FOREIGN_COR: return "converted firmware";
    case FWK_OUR_COR: return "large core";
    }
    return "?";
}

fw_result_t fw_probe(CorSource &src, uint32_t running_idcode, FwPkg *p)
{
    memset(p, 0, sizeof(*p));
    p->kind = FWK_NONE;
    p->file_size = src.size();
    p->probe = cfw_probe(src, running_idcode, p->ci);
    if (p->probe != CFW_OK) {
        return p->probe == CFW_ERR_IO ? FW_ERR_IO : FW_ERR_PROBE;
    }
    const CfwInfo &ci = p->ci;
    snprintf(p->name, sizeof(p->name), "%s", ci.name);
    p->bit_flash_len = ci.bit_len;
    p->app_flash_len = FW_REC_HDR + pad4(ci.app_len);
    if (ci.from_cor) {
        static CorInfo c;
        cor_result_t r = cor_probe(src, c);
        if (r != COR_ERR_LARGE) {
            return FW_ERR_NOT_FIRMWARE;
        }
        p->origin = c.origin;
        snprintf(p->version, sizeof(p->version), "%s", c.version);
        snprintf(p->name, sizeof(p->name), "%s", c.name);
        snprintf(p->id, sizeof(p->id), "%s", c.id);
        snprintf(p->flash_id, sizeof(p->flash_id), "%s", c.flash_id);
        p->kind = c.origin == COR_ORIGIN_BACKUP ? FWK_BACKUP_COR : (c.origin == COR_ORIGIN_FOREIGN ? FWK_FOREIGN_COR : FWK_OUR_COR);
        return FW_OK;
    }
    p->kind = FWK_FIRMWARE;
    return FW_OK;
}

bool fw_can_restore(const FwPkg &p, const FwKnown *known, const char *chip)
{
    switch (p.kind) {
    case FWK_BACKUP_COR: return chip && !strcmp(p.flash_id, chip);
    case FWK_FIRMWARE: return known != 0;
    default: return false;
    }
}

static bool fail(FwCheck *o, const char *why)
{
    o->ok = false;
    o->why = why;
    return false;
}

bool fw_restore_stage(CorSource &src, const FwPkg &p, const FwKnown *known, const char *chip, uint8_t *bit_dst, uint8_t *app_dst, FwStageProgress cb, void *ctx,
                      bool (*abort)(void *), FwCheck *o, FwImage *img)
{
    memset(o, 0, sizeof(*o));
    memset(img, 0, sizeof(*img));
    o->bit_len = p.bit_flash_len;
    o->app_len = p.app_flash_len;
    if (p.kind == FWK_NONE) {
        return fail(o, "not a firmware");
    }
    if (!fw_can_restore(p, known, chip)) {
        return fail(o, "this file is not one that may be restored");
    }
    if (p.bit_flash_len > FW_BIT_SLOT_FULL) {
        return fail(o, "the bitstream is larger than its slot");
    }
    if (p.app_flash_len > FW_APP_SLOT) {
        return fail(o, "the application is larger than its slot");
    }
    const CfwInfo &ci = p.ci;
    uint32_t total = ci.bit_len + ci.app_len, done = 0;
    FwHash hb, ha;
    fw_hash_begin(&hb, 0);
    if (!fw_read_part(src, ci.bit_off, bit_dst, ci.bit_len, ci.bit_len, hb, cb, ctx, &done, total, abort)) {
        return fail(o, abort && abort(ctx) ? "cancelled" : "the file cannot be read");
    }
    fw_hash_begin(&ha, 0);
    if (ci.from_cor) {
        // A .cor holds the raw image: the record is made here (load = start = 0x30000, T115 section 7)
        uint8_t rec[FW_REC_HDR] = { 0 };
        for (int i = 0; i < 4; i++) {
            rec[i] = (uint8_t)(CFW_APP_ADDR >> (8 * i));
            rec[8 + i] = rec[i];
            rec[4 + i] = (uint8_t)(ci.app_len >> (8 * i));
        }
        memcpy(app_dst, rec, FW_REC_HDR);
        if (!fw_read_part(src, ci.app_data, app_dst + FW_REC_HDR, pad4(ci.app_len), ci.app_len, ha, cb, ctx, &done, total, abort)) {
            return fail(o, abort && abort(ctx) ? "cancelled" : "the file cannot be read");
        }
        sha256_t s;
        uint8_t d[32];
        sha256_init(&s);
        sha256_update(&s, app_dst + FW_REC_HDR, ci.app_len);
        sha256_final(&s, d);
        sha256_hex(d, o->app_sha);
    } else {
        // the record and its data: 12 + pad4(len) bytes; a vendor file may end right behind the data
        uint32_t have = FW_REC_HDR + ci.app_len;
        if (ci.app_off + FW_REC_HDR + pad4(ci.app_len) <= p.file_size) {
            have = FW_REC_HDR + pad4(ci.app_len);
        }
        if (!fw_read_part(src, ci.app_off, app_dst, p.app_flash_len, have, ha, cb, ctx, &done, total, abort)) {
            return fail(o, abort && abort(ctx) ? "cancelled" : "the file cannot be read");
        }
        fw_hash_sha_hex(&ha, o->app_sha);
        if (app_dst[0] != (uint8_t)CFW_APP_ADDR || app_dst[1] != (uint8_t)(CFW_APP_ADDR >> 8) || app_dst[2] != (uint8_t)(CFW_APP_ADDR >> 16) ||
            app_dst[8] != app_dst[0] || app_dst[9] != app_dst[1] || app_dst[10] != app_dst[2]) {
            return fail(o, "the application does not load and start at 0x30000");
        }
    }
    fw_hash_sha_hex(&hb, o->bit_sha);
    if (ci.from_cor && (strcmp(o->bit_sha, ci.bit_sha) || strcmp(o->app_sha, ci.app_sha))) {
        return fail(o, "a part does not match the SHA-256 in its manifest");
    }
    uint32_t ax = 0;
    o->bit_axss = fw_axss(bit_dst, p.bit_flash_len, &ax) ? ax : 0;
    if (ourfw_is_ours(o->bit_axss != 0, ourfw_app_marked(app_dst + FW_REC_HDR, p.app_flash_len > FW_REC_HDR ? p.app_flash_len - FW_REC_HDR : 0))) {
        return fail(o, "this is MultiCore's own firmware, not the one that ran before it");   // T135 B2: a backup read from a flash that held our host
    }
    const char *why = "";
    if (!fw_image_seal(img, true, bit_dst, p.bit_flash_len, app_dst, p.app_flash_len, 0, &why)) {
        return fail(o, why);
    }
    o->ok = true;
    o->why = "";
    return true;
}
