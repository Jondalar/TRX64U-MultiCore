// T265 P2/P3: the application's side of the guard (guard.h). Pure code over the flash layer and FoFs.
#include "guard.h"
#include "guardimg.h"
#include "flash.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static_assert(GUARDIMG_BLK_ADDR == GUARD_BLK_ADDR && GUARDIMG_RECOVERY_MAGIC == GUARD_RECOVERY_MAGIC && GUARDIMG_RECORD_ADDR == FW_RECORD_ADDR &&
              GUARDIMG_RECORD_MAGIC == FW_GUARD_MAGIC && GUARDIMG_APP_SLOT == FW_APP_SLOT, "the recovery program and the application disagree on the guard's numbers");

static uint32_t le32(const uint8_t *p) { return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

// ---- the blocks ----------------------------------------------------------------------------------------------------------------------------------
uint32_t guard_recovery_chk(const uint32_t *blk)
{
    uint32_t x = 0;
    for (int i = 0; i < 7; i++) {
        x ^= blk[i];
    }
    return ~x;
}

void guard_decode(const uint32_t *w, GuardInfo *g)
{
    memset(g, 0, sizeof(*g));
    g->start = GS_OTHER;
    const uint32_t *gb = w + GUARD_W_GUARD, *rb = w + GUARD_W_RECOVERY, *res = w + GUARD_W_RESULT;
    if (res[0] != GUARD_RES_VALID || gb[0] != GUARD_BLOCK_MAGIC) {
        return;
    }
    g->verdict = gb[1];
    g->rec_len = gb[2];
    g->rec_crc = gb[3];
    g->flash_len = gb[4];
    g->flash_crc = gb[5];
    g->iface = gb[6];
    g->rec_status = gb[7];
    if (res[1] == GUARD_ST_BOOTING && res[2] == GUARD_SRC_FLASH) {
        g->start = GS_FLASH;
        return;
    }
    if (res[1] == GUARD_ST_GUARD && rb[0] == GUARD_RECOVERY_MAGIC && rb[7] == guard_recovery_chk(rb)) {
        g->start = GS_RECOVERY;
        g->medium = rb[1];
        g->stage_addr = rb[2];
        g->stage_len = rb[3];
        g->stage_crc = rb[4];
        g->copy_matches_record = rb[5];
    }
}

// ---- sources -------------------------------------------------------------------------------------------------------------------------------------
bool GuardMem::read(uint32_t off, void *buf, uint32_t len)
{
    if ((uint64_t)off + len > n) {
        return false;
    }
    memcpy(buf, p + off, len);
    return true;
}

bool GuardFlashApp::read(uint32_t off, void *buf, uint32_t len)
{
    if ((uint64_t)off + len > n) {
        return false;
    }
    static uint8_t pg[FW_PAGE] __attribute__((aligned(4)));
    uint8_t *d = (uint8_t *)buf;
    while (len) {
        uint32_t a = FW_APP_ADDR + off, in = a % FW_PAGE, c = FW_PAGE - in < len ? FW_PAGE - in : len;
        if (!fl->read_page((int)(a / FW_PAGE), pg)) {
            return false;
        }
        memcpy(d, pg + in, c);
        d += c;
        off += c;
        len -= c;
    }
    return true;
}

// ---- the recovery copy ---------------------------------------------------------------------------------------------------------------------------
static FoFs *g_fs;
void guard_install_fs(FoFs *fs) { g_fs = fs; }

#define COPY_CHUNK 4096u
static uint8_t g_buf[COPY_CHUNK] __attribute__((aligned(4)));
#define COPY_SLACK 65536u         // the directory and the FAT beside the data

static bool stamp_write(uint32_t len, uint32_t crc)
{
    char t[40];
    int n = snprintf(t, sizeof(t), "%u %u\n", (unsigned)len, (unsigned)crc);
    void *h = g_fs->open_write(GUARD_STAMP_PATH);
    if (!h) {
        return false;
    }
    bool ok = g_fs->write(h, t, (uint32_t)n);
    return g_fs->close(h) && ok;
}

static bool stamp_read(uint32_t *len, uint32_t *crc)
{
    void *h = g_fs->open_read(GUARD_STAMP_PATH);
    if (!h) {
        return false;
    }
    char t[40] = { 0 };
    int n = g_fs->read(h, t, sizeof(t) - 1);
    g_fs->close(h);
    char *e1 = 0, *e2 = 0;
    unsigned long l = strtoul(t, &e1, 10), c = e1 && *e1 == ' ' ? strtoul(e1 + 1, &e2, 10) : 0;
    if (n <= 0 || e1 == t || !e2 || e2 == e1 + 1) {
        return false;
    }
    *len = (uint32_t)l;
    *crc = (uint32_t)c;
    return true;
}

bool guard_copy_current(uint32_t len, uint32_t crc)
{
    uint32_t l, c;
    FoStat st;
    return g_fs && stamp_read(&l, &c) && l == len && c == crc && g_fs->stat(GUARD_COPY_PATH, &st) && !st.dir && st.size == len;
}

void guard_copy_drop(void)
{
    if (!g_fs) {
        return;
    }
    FoStat st;
    if (g_fs->stat(GUARD_STAMP_PATH, &st)) {
        g_fs->remove(GUARD_STAMP_PATH);                 // the stamp first: a copy without a stamp is rewritten, a stamp without a copy is not believed
    }
    if (g_fs->stat(GUARD_COPY_PATH, &st)) {
        g_fs->remove(GUARD_COPY_PATH);
    }
    if (g_fs->stat(GUARD_NEW_PATH, &st)) {
        g_fs->remove(GUARD_NEW_PATH);
    }
}

bool guard_copy_write(CorSource &src, uint32_t len, uint32_t crc, const char **why, bool *room)
{
    *room = false;
    if (!g_fs) {
        *why = "no file system for the recovery copy";
        return false;
    }
    g_fs->make_dir(GUARD_DIR);
    uint64_t have = 0;
    if (!g_fs->free_bytes(GUARD_DIR, &have)) {
        *why = "the flash drive cannot be read";
        return false;
    }
    if (have < (uint64_t)len + COPY_SLACK) {
        *room = true;
        *why = "the flash drive has no room for the recovery copy";
        return false;
    }
    void *h = g_fs->open_write(GUARD_NEW_PATH);
    if (!h) {
        *why = "the recovery copy cannot be created";
        return false;
    }
    bool ok = true;
    for (uint32_t off = 0; ok && off < len; off += COPY_CHUNK) {
        uint32_t n = len - off < COPY_CHUNK ? len - off : COPY_CHUNK;
        ok = src.read(off, g_buf, n) && g_fs->write(h, g_buf, n);
    }
    ok = g_fs->close(h) && ok;
    if (!ok) {
        g_fs->remove(GUARD_NEW_PATH);
        *why = "the recovery copy could not be written";
        return false;
    }
    // read back: the file as the medium keeps it, by CRC-32
    h = g_fs->open_read(GUARD_NEW_PATH);
    uint32_t c = 0, got_total = 0;
    int got = 0;
    while (h && (got = g_fs->read(h, g_buf, COPY_CHUNK)) > 0) {
        c = fw_crc32_update(c, g_buf, (uint32_t)got);
        got_total += (uint32_t)got;
    }
    if (h) {
        g_fs->close(h);
    }
    if (!h || got < 0 || got_total != len || c != crc) {
        g_fs->remove(GUARD_NEW_PATH);
        *why = "the recovery copy does not read back as written";
        return false;
    }
    FoStat st;
    g_fs->remove(GUARD_STAMP_PATH);                     // the stamp of the old copy must not name the new one
    if (g_fs->stat(GUARD_COPY_PATH, &st) && !g_fs->remove(GUARD_COPY_PATH)) {
        *why = "the old recovery copy cannot be replaced";
        return false;
    }
    if (!g_fs->rename(GUARD_NEW_PATH, GUARD_COPY_PATH)) {
        *why = "the recovery copy cannot get its name";
        return false;
    }
    if (!stamp_write(len, crc)) {
        *why = "the recovery copy's stamp cannot be written";
        return false;
    }
    return true;
}

// ---- the write order -----------------------------------------------------------------------------------------------------------------------------
void guard_finish(Flash *fl, CorSource &app, uint32_t len, uint32_t crc, bool copy_is_there, GuardOut *o)
{
    memset(o, 0, sizeof(*o));
    o->copy_why = o->record_why = "";
    if (copy_is_there) {
        o->copy_ok = stamp_write(len, crc);               // the file is the stage itself: only the stamp is brought up to date
        if (!o->copy_ok) {
            o->copy_why = "the recovery copy's stamp cannot be written";
        }
    } else {
        o->copy_ok = guard_copy_write(app, len, crc, &o->copy_why, &o->copy_room);
        if (!o->copy_ok) {
            guard_copy_drop();                            // an older copy must not outlive the new record
        }
    }
    FwWriteResult r;
    o->record_ok = fw_write_record(fl, len, crc, &r);
    if (!o->record_ok) {
        o->record_why = r.why;
    }
}

bool guard_active(void) { return g_fs != 0; }

bool guard_after_update(Flash *fl, const uint8_t *app, uint32_t len, GuardOut *o)
{
    if (!g_fs) {
        return false;
    }
    GuardMem src(app, len);
    guard_finish(fl, src, len, fw_crc32(app, len), false, o);
    return true;
}

// ---- provisioning --------------------------------------------------------------------------------------------------------------------------------
static void say(const GuardEnv &e, const char *a, const char *b)
{
    if (e.log) {
        char l[160];
        snprintf(l, sizeof(l), "[host] guard: %s%s", a, b ? b : "");
        e.log(l);
    }
}

bool guard_provision(const GuardEnv &env, const GuardInfo &gi, GuardProv *o)
{
    memset(o, 0, sizeof(*o));
    o->why = "";
    Flash *fl = env.fl;
    // the application in the flash must be the one that runs: its image and CRC-32 are the boot ROM's (when it read them) or are made here
    uint32_t len = 0, crc = 0;
    if (!fw_flash_app_crc(fl, &len, &crc)) {
        o->why = "the application in the flash cannot be read";
        say(env, o->why, 0);
        return false;
    }
    if (gi.flash_crc != 0 && (gi.flash_len != len || gi.flash_crc != crc)) {
        o->why = "the application in the flash is not the one that runs";
        say(env, o->why, 0);
        return false;
    }
    o->app_ok = true;
    bool all = true;
    // 1. the recovery program
    if (env.rec_img && env.rec_len) {
        o->rec_checked = true;
        if (!fw_recovery_equals(fl, env.rec_img, env.rec_len)) {
            FwWriteResult r;
            if (fw_write_recovery(fl, env.rec_img, env.rec_len, &r)) {
                o->rec_written = true;
                say(env, "the recovery program was written", 0);
            } else {
                o->rec_failed = true;
                all = false;
                o->why = r.why;
                say(env, "the recovery program could not be written: ", r.why);
            }
        }
    }
    // 2. the copy
    if (!guard_copy_current(len, crc)) {
        GuardFlashApp src(fl, len);
        bool room = false;
        const char *why = "";
        if (guard_copy_write(src, len, crc, &why, &room)) {
            o->copy_written = true;
            say(env, "the recovery copy was written", 0);
        } else {
            o->copy_failed = true;
            o->copy_room = room;
            all = false;
            if (!o->why[0]) {
                o->why = why;
            }
            say(env, "the recovery copy could not be written: ", why);
            guard_copy_drop();
        }
    }
    // 3. the record
    FwGuardRecord rec;
    if (!fw_read_record(fl, &rec) || rec.len != len || rec.crc != crc) {
        FwWriteResult r;
        if (fw_write_record(fl, len, crc, &r)) {
            o->record_written = true;
            say(env, "the record was written", 0);
        } else {
            o->record_failed = true;
            all = false;
            if (!o->why[0]) {
                o->why = r.why;
            }
            say(env, "the record could not be written: ", r.why);
        }
    }
    return all;
}

// ---- repair --------------------------------------------------------------------------------------------------------------------------------------
bool guard_repair_check(const GuardInfo &gi, uint32_t iface_needed, const uint8_t *stage, FwImage *img, const char **why)
{
    memset(img, 0, sizeof(*img));
    if (gi.start != GS_RECOVERY || !stage) {
        *why = "this application was not started by the recovery program";
        return false;
    }
    if (gi.iface != iface_needed) {
        *why = "this application needs another host interface than the host in the flash provides";
        return false;
    }
    if (gi.stage_len < FW_REC_HDR + 4 || gi.stage_len > FW_APP_SLOT || fw_crc32(stage, gi.stage_len) != gi.stage_crc) {
        *why = "the image the recovery program started is not intact";
        return false;
    }
    return fw_image_seal_parts(img, true, 0, 0, stage, gi.stage_len, 0, why);
}

void guard_repair_finish(Flash *fl, const GuardInfo &gi, const FwImage &img, GuardOut *o)
{
    GuardMem src(img.app, img.app_len);
    guard_finish(fl, src, img.app_len, gi.stage_crc, gi.medium == GM_FLASH_COPY, o);
}
