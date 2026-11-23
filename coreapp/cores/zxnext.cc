// The ZX Spectrum Next descriptor (T116, T104): no ROMs - the Next's boot ROM is in its source and the operating system and the ROMs it loads are
// on the user's SD card image. The image is a plain file (a whole FAT card, block n at byte n x 512) chosen in the file browser, or
// roms/zxnext/next.img on the first root that has it; the card is served block by block through the sector mailbox (sectors.h) and every block the
// Next writes goes straight back into the file. The mouse is a counter sink (mousesink.h). The real-time clock the Next reads from its DS1307
// is the application's: the service writes the time into the register block once a second. The register block is cores/zxnext/trx/zxn_regs.v;
// the picture is the core's own frame writer into the DDR2 (640 x 512, four buffers) which the host's pixel layer shows.
#include "hk_defaults.h"
#include "memops.h"
#include "boot.h"
#include "engines.h"
#include "mousesink.h"
#include "romfind.h"
#include "zxnext_regs.h"
#include "cores.h"
#include "zxnext_fw.h"
#include "sectors.h"
#include <stdio.h>
#include <string.h>

// ---- the machine's state in the application --------------------------------------------------------------------------------
struct ZxnState {
    bool rtc_set;
    uint32_t rtc_ms;
    uint8_t last_cmd;                             // the card model's last command as the log last said it
    uint8_t cmd_lines;                            // lines of that log so far
    bool default_done;                            // the default image is mounted, or settled otherwise: no more looking
    bool default_told;                            // "No SD image" has been said
    uint32_t default_ms;                          // the last look of the service
};

static ZxnState *zxn_state(CoreCtx *c)
{
    if (!c->state) {
        c->state = new ZxnState();
        memset(c->state, 0, sizeof(ZxnState));
    }
    return (ZxnState *)c->state;
}

static void wr32(CoreCtx *c, uint32_t reg, uint32_t v)
{
    for (int i = 0; i < 4; i++) {
        c->io->wr(reg + i, (uint8_t)(v >> (8 * i)));
    }
}

// ---- settings ----------------------------------------------------------------------------------------------------------------
static const char *const joy_names[]  = { "Normal", "Swapped" };
static const char *const lock_names[] = { "On", "Off" };       // the value is the LOCK register's bit: 0 = locked
static int op_joy(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(ZXN_JOY, (uint8_t)a.i(0)); return OPE_OK; }
static int op_lock(CoreCtx *c, const OpArgs &a, OpOut &) { c->io->wr(ZXN_LOCK, (uint8_t)a.i(0)); return OPE_OK; }
static const ArgSpec flag_arg[] = { ARG_INT("value", 0, 1) };

// The board issue the machine reports in NextReg 0x0F (T116 D10, D11): the id is the issue's number minus 2 (2 -> 0, 4 -> 2, 5 -> 3), written to the
// BOARD_ID register while the core is held, before the boot ROM runs. Only the report changes; the machine's structure stays issue 5. "Auto" (the
// default) takes the newest id the TBBLUE.FW on the mounted SD image accepts (zxnext_fw.h), as far as the machine has the structure for it (3); an
// image it cannot read that from is reported as issue 5, with a notice.
enum { BOARD_AUTO = 0, BOARD_ID_MAX = 3 };
static const char *const board_names[] = { "Auto", "Issue 2", "Issue 4", "Issue 5" };
static const uint8_t board_ids[4] = { BOARD_ID_MAX, 0, 2, 3 };         // the first is the fallback of Auto

static uint8_t auto_board(CoreCtx *c)
{
    SecUnit &u = engines_of(c)->sec.unit[0];
    if (!u.mounted || u.folder || !u.file) {
        return board_ids[BOARD_AUTO];                  // no image (the "No SD image" notice has been given): nothing to read
    }
    int maxid = -1;
    int rc = zxfw_board_max(u.file, &maxid);
    if (rc == ZXFW_OK) {
        uint8_t id = (uint8_t)(maxid > BOARD_ID_MAX ? BOARD_ID_MAX : maxid);
        printf("[zxnext] board issue Auto: id %u (the firmware of %s takes up to %d)\n", (unsigned)id, u.name, maxid);
        return id;
    }
    snprintf(c->notice, sizeof(c->notice), "Board issue Auto: %s in %s, reported as Issue 5 - choose the issue in the Settings if the machine stops",
             rc == ZXFW_NO_FILE ? "no TBBLUE.FW" : "no board table in TBBLUE.FW", u.name);
    c->notice_pending = true;
    return board_ids[BOARD_AUTO];
}

static int op_board(CoreCtx *c, const OpArgs &a, OpOut &)
{
    int v = a.i(0);
    c->io->wr(ZXN_BOARD_ID, v == BOARD_AUTO ? auto_board(c) : board_ids[v]);
    return OPE_OK;
}
static const ArgSpec board_arg[] = { ARG_INT("value", 0, 3) };

static const Setting settings[3] = {
    { "Joystick ports",        joy_names,  2, 0, WHEN_LIVE, "zxnext.set_joy" },
    { "Frame lock to 50 Hz",   lock_names, 2, 0, WHEN_LIVE, "zxnext.set_lock" },
    { "Board issue",           board_names, 4, BOARD_AUTO, WHEN_RESET, "zxnext.set_board" },
};

// ---- control -----------------------------------------------------------------------------------------------------------------
// The card's image: <core folder>/roms/next.img (T148), else the first roms/zxnext/next.img of the roots, when nothing has been chosen. The core application starts before the stick is
// mounted (coreapp.cc re-initialises the USB stack, libboard.cc waits for it for the libraries and the ROM scan of other cores waits for it; this core
// has no ROM to wait for), so a single look at the start finds nothing and the machine's boot ROM stops with "Error initializing SD card!". The look is
// repeated by the service once a second until the image is there, and the machine starts over with it (the boot ROM reads the card once).
// Returns true when the matter is settled: the image is mounted, or the file is one the core cannot take (told once). False: not there yet.
static bool mount_default(CoreCtx *c)
{
    ZxnState *st = zxn_state(c);
    SecState &sec = engines_of(c)->sec;
    if (sec.unit[0].mounted) {
        return true;                                // the user chose an image: the default is not asked for
    }
    char path[120], msg[120], own[96];
    bool have_own = rom_own_dir(c->cor_path, own, sizeof(own));     // T148: the core's own folder first; an image of the old places is used where it lies (D6)
    for (int r = have_own ? -1 : 0; r < 0 || rom_roots[r]; r++) {
        if (r < 0) {
            snprintf(path, sizeof(path), "%s/next.img", own);
        } else {
            snprintf(path, sizeof(path), "%s/roms/zxnext/next.img", rom_roots[r]);
        }
        uint64_t size;
        bool dir;
        if (c->fs->stat(path, &size, &dir) && !dir) {
            int rc = sec_mount(c, 0, path, msg, sizeof(msg));
            if (!rc) {
                printf("[zxnext] SD image %s\n", path);
                return true;
            }
            if (rc == 2) {
                return false;                       // cannot open it (yet): the medium is still coming up
            }
            snprintf(c->notice, sizeof(c->notice), "%s", msg);
            c->notice_pending = true;
            return true;
        }
    }
    if (!st->default_told) {
        st->default_told = true;
        if (have_own) {
            snprintf(c->notice, sizeof(c->notice), "No SD image: put next.img in %s/, or choose a .img in Library (F3), then Reset", own);
        } else {
            snprintf(c->notice, sizeof(c->notice), "No SD image: put next.img in roms/zxnext on a stick or the card, or choose a .img in Library (F3), then Reset");
        }
        c->notice_pending = true;
    }
    return false;
}

static int zxn_init(CoreCtx *c, char *, int)
{
    ZxnState *st = zxn_state(c);
    engines_before_hold(c);                       // what the Next wrote to the card is in the file before the machine starts over
    c->io->wr(ZXN_CTRL, ZXN_HOLD);
    c->io->mode_reset(true);
    wr32(c, ZXN_RAM_BASE, 0x01000000u + ZXN_W_RAM);
    wr32(c, ZXN_STAGE_BASE, 0x01000000u + ZXN_W_STAGE);
    if (!st->default_done) {                      // once: an image that was ejected stays ejected over a Reset
        st->default_done = mount_default(c);
    }
    engines_after_hold(c);
    return 0;
}

static int zxn_release(CoreCtx *c, char *, int)
{
    c->io->wr(ZXN_CTRL, 0);
    c->io->mode_reset(false);
    return 0;
}

// ---- the card, the clock, the keys ---------------------------------------------------------------------------------------------
static uint8_t bcd(unsigned v) { return (uint8_t)(((v / 10) << 4) | (v % 10)); }

static void write_rtc(CoreCtx *c)
{
    uint8_t t[6];                                  // year 0-99, month, day, hour, minute, second
    if (!c->io->wall_time(t)) {
        // no clock on the board: a fixed date that moves on with the uptime, so that files at least get increasing stamps
        uint32_t s = c->io->now_ms() / 1000;
        t[0] = 26; t[1] = 1; t[2] = 1; t[3] = (uint8_t)((s / 3600) % 24); t[4] = (uint8_t)((s / 60) % 60); t[5] = (uint8_t)(s % 60);
    }
    static const uint8_t mt[12] = { 0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4 };
    unsigned y = 2000u + t[0], m = t[1] ? t[1] : 1;
    unsigned yy = m < 3 ? y - 1 : y;
    unsigned dow = (yy + yy / 4 - yy / 100 + yy / 400 + mt[m - 1] + t[2]) % 7;          // 0 = Sunday
    uint8_t regs[7] = { bcd(t[5] % 60), bcd(t[4] % 60), bcd(t[3] % 24), (uint8_t)(dow + 1), bcd(t[2]), bcd(m), bcd(t[0]) };
    for (int i = 0; i < 7; i++) {
        c->io->wr(ZXN_RTC + i, regs[i]);
    }
}

// The service: the sector mailbox (a card is read block after block, so a pass that served any asks to be called again at once), the idle sync of
// a written image, the mouse, the default image until it is there, and the clock once a second.
static int op_service(CoreCtx *c, const OpArgs &, OpOut &o)
{
    engines_of(c);
    if (sec_service(c, 16)) {
        o.more = true;
    }
    sec_idle(c);
    mouse_sink_pace(c);
    ZxnState *st = zxn_state(c);
    {   // what the machine asks of its card, for the log: a change of the last command, 100 lines at most (which commands the Next's firmware uses is not in this repository)
        uint8_t lc = c->io->rd(ZXN_SD_LASTCMD);
        if (lc != st->last_cmd && st->cmd_lines < 100) {
            st->last_cmd = lc;
            st->cmd_lines++;
            printf("[zxnext] SD %s%u\n", (lc & 0x80) ? "ACMD" : "CMD", (unsigned)(lc & 0x7F));
        }
    }
    uint32_t now = c->io->now_ms();
    if (!st->default_done && now - st->default_ms >= 1000) {
        st->default_ms = now;
        SecState &sec = c->eng->sec;
        if (sec.unit[0].mounted) {
            st->default_done = true;              // the user chose an image meanwhile (Mount resets, Mount alone does not): nothing to start over
        } else if (mount_default(c)) {
            st->default_done = true;
            if (sec.unit[0].mounted) {            // the stick came up after the start: the boot ROM has given up on the card, start over with it
                char msg[120];
                boot_cold_reset(c, msg, sizeof(msg));
            }
        }
    }
    if (!st->rtc_set || now - st->rtc_ms >= 1000) {
        write_rtc(c);
        st->rtc_set = true;
        st->rtc_ms = now;
    }
    return OPE_OK;
}

// Choose an image and start over with it.
static int op_mount_reset(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (sec_mount(c, 0, a.s(0), o.msg, o.msglen)) {
        return OPE_FAILED;
    }
    return boot_cold_reset(c, o.msg, o.msglen) ? OPE_FAILED : OPE_OK;
}

// A press of one of the Next's special keys (F3 50/60 Hz, F8 CPU speed) or of its Multiface button: the register is held for 60 ms.
static int op_fkey(CoreCtx *c, const OpArgs &a, OpOut &)
{
    int n = a.i(0);
    c->io->wr(ZXN_FKEYS + (n >> 3), (uint8_t)(1 << (n & 7)));
    c->io->delay_ms(60);
    c->io->wr(ZXN_FKEYS + (n >> 3), 0);
    return OPE_OK;
}
static int op_nmi_m1(CoreCtx *c, const OpArgs &, OpOut &)
{
    c->io->wr(ZXN_BUTTONS, 1);
    c->io->delay_ms(60);
    c->io->wr(ZXN_BUTTONS, 0);
    return OPE_OK;
}
static const ArgSpec key_arg[] = { ARG_INT("key", 0, 9) };
static const ArgSpec mount_args[] = { ARG_PATH("path") };

static const SectorMediaSpec sd_card = {
    "SD", 1, ZXN_SD_MOUNT, 8, ZXN_FEAT, 0x01, ZXN_MBOX_STATUS, ZXN_MBOX_LBA, ZXN_MBOX_DONE, ZXN_W_STAGE, 1000, 0xF0,
};
static const MouseSinkSpec mouse = { ZXN_MOUSE_DX, ZXN_MOUSE_DY, ZXN_MOUSE_BTN, 4000, 127, 2, MOUSE_COUNTERS };

static const OpBind sd0[] = { B_INT("unit", 0) };
static const OpBind f3[] = { B_INT("key", ZXN_FKEY_F3) }, f8[] = { B_INT("key", ZXN_FKEY_F8) };
static const OpBind mnt_reset[] = { B_FILE("path") };
static const OpBind mnt[] = { B_FILE("path"), B_INT("unit", 0) };

static const Task tasks[] = {
    { "Reset",                        "machine.cold_reset", 0,  0, true },
    { "Eject the SD image",           "sectors.eject",      sd0, 1, false },
    { "F3: 50 / 60 Hz",               "zxnext.fkey",        f3, 1, false, 0, 0, false, 0, "machine" },
    { "F8: CPU speed",                "zxnext.fkey",        f8, 1, false, 0, 0, false, 0, "machine" },
    { "NMI: Multiface (M1 button)",   "zxnext.nmi_m1",      0,  0, true },
};

static const FileAction img_actions[] = {
    { "Mount SD image and reset", "zxnext.mount_reset", mnt_reset, 1, true },
    { "Mount SD image",           "sectors.mount",      mnt,       2, false },
};
static const FileType filetypes[] = { { ".img", img_actions, 2 } };

static const MediaSlot media[] = {
    { "SD", "zxnext.sd.image", "sectors.eject", sd0, 1, 0, ".img", "Mount SD image", 0, 0 },
};

static const MemRegion zxnext_mem_regions[] = { { 0, 0x200000, MK_DDR, ZXN_W_RAM, 0, "RAM (2 MB)" } };
static const MemMap zxnext_mem = { zxnext_mem_regions, 1, false, 0 };

static void zxn_register(OpRegistry &r)
{
    r.contributor("zxnext");
    MEM_OPS(r, zxnext_mem, 0x1FFFFF);
    r.add_op("zxnext.set_joy", flag_arg, 1, op_joy);
    r.add_op("zxnext.set_lock", flag_arg, 1, op_lock);
    r.add_op("zxnext.set_board", board_arg, 1, op_board);
    r.add_op("zxnext.fkey", key_arg, 1, op_fkey);
    r.add_op("zxnext.nmi_m1", 0, 0, op_nmi_m1);
    r.add_op("zxnext.mount_reset", mount_args, 1, op_mount_reset);
    r.add_op("zxnext.service", 0, 0, op_service);
    r.add_state("zxnext.sd.image", sec_state<0>);
}

// A card and a mouse; no floppy, no tape, no cartridge: a library of another machine's media is refused here.
static const char *const caps[] = { "sdcard", "mouse" };

extern const CoreDesc zxnext_desc;
const CoreDesc zxnext_desc = {
    { 'Z', 'X', 'N', '_' },
    "ZX Spectrum Next",
    "ZX Spectrum Next",
    "NEXT",
    "zxnext",
    "ZX Spectrum Next Settings",
    1,
    0, 0,
    settings, 3,
    tasks, 5,
    filetypes, 1,
    media, 1,
    // 640 x 512 at scale 2 is 1280 x 1024, centred on 1920 x 1080 (T116 D5): a Next dot is half as wide as a line is tall, every line is written twice
    { 640, 512, 2, 320, 28, 0x01000000u + ZXN_W_FB, 0x100000u, 4, ZXN_FB_LATEST, ZXN_FRAME_SEQ, ZXN_FB_BASE, ZXN_FB_STRIDE },
    { ZXN_STATUS, ZXN_ST_HELD, 200 },
    zxn_init, zxn_release, 0, 20,
    "zxnext.service", 2, false,
    0, 0,
    zxn_register,
    caps, 2,
    false,
    false, 0, 0, 0, 0,
    &sd_card, &mouse, 0,
    0, 0, 0, 0, 0, 0, 0,                       // type_script, run_prg, cold_ram_bytes, cold_start, id_feat_reg, id_feat_mask, id_feat_val
    0, 0, 0, 0, 0,                             // menu_rec, menu_rec_n, core_id, report_regs, n
    0, false, 0, 0, false,                     // rawkeys, own_volume, pad, live_ms, menu_first
    0, 0, 0,                                   // media_flush, media_pending, mount_slots
    &hk_typing,                                // T181: the default hotkey map (hk_defaults.h)
};
