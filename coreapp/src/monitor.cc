// The monitor of F4: trxmon's engine in the application (monitor.h). PROVENANCE: the trx table (acquire, release, the ring and the video calls) is api_trx_* of
// vendor/gideon/software/app_loader/app_api_net.cc (Gideon Zweijtzer's firmware with the TRX64 line's additions, GPL-3.0) with the frame ring being this application's
// (ring.cc); the platform calls are what trxmon's main.c and the firmware's app API gave it, here over CoreFs and FreeRTOS; the session is ui_session() of trxmon's main.c
// (ui_set_size, ui_set_keys, ui_open, ui_map_key, ui_key, ui_tick, ui_render) without the overlay; the RPC server is monsrv.cc.
#include "monitor.h"
#include "ops_glue.h"
#include "fsif.h"
#include "trx_ring.h"
#include "monsrv.h"
#include "trx_io.h"
#include "keyboard.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "u2p_app_api.h"

extern "C" {
#include "trxmon/hw.h"
#include "trxmon/mach.h"
#include "trxmon/mon.h"
#include "trxmon/rpc.h"
#include "trxmon/ui.h"
}

#define TRX_P0(off)  (TRX_IO_BASE + (off))
#define TRX_P3(off)  (TRX_IO_BASE + 0x3000 + (off))
#define TRX_CAPS_DBG 0x08                       // page 3: debug (trx_dbg.v, drv_dbg.v)

static CoreCtx *g_c;

// ---- the trx table of the firmware's app API ---------------------------------------------------------------------------------------------------------------
static bool s_owned;

static int api_trx_acquire(uint32_t)
{
    trx_io_write32(TRX_P3(0x000), 0x01);        // PAUSE_CB: the machine is held first, then the app takes run control from C64_STOP
    trx_io_write32(TRX_P0(0x118), 1);           // OWNER
    s_owned = true;
    return U2P_OK;
}

static void api_trx_release(void)
{
    if (!s_owned) {
        return;
    }
    trx_io_write32(TRX_P3(0x048), 0);           // DBG_CTRL: exec bitmap off
    trx_io_write32(TRX_P3(0x080), 0);           // watch unit 0 off
    trx_io_write32(TRX_P3(0x0A0), 0);           // watch unit 1 off
    trx_io_write32(TRX_P3(0x0C0), 0);           // drive comparator off
    trx_io_write32(TRX_P0(0x134), 0);           // CYC_CMP disarmed
    trx_io_write32(TRX_P0(0x118), 0);           // OWNER: C64_STOP rules the gate again
    trx_io_write32(TRX_P3(0x000), 0x03);        // RUN: drop the debugger's hold and the stop record
    s_owned = false;
}

static void api_trx_announce(uint16_t) { }

static int api_ring_status(u2p_ring_status_t *st)
{
    TrxRingStatus rs;
    trx_ring_status(&rs);
    memset(st, 0, sizeof(*st));
    st->present = rs.present;
    st->on = rs.on;
    st->reu_off = 0;
    st->frames = rs.frames;
    st->targets = rs.targets;
    st->head_seq = rs.head_seq;
    st->oldest_seq = rs.oldest_seq;
    st->landed_seq = trx_ring_landed();
    st->landed = st->landed_seq != 0;
    return rs.present ? U2P_OK : U2P_ERR_UNSUPPORTED;
}

static int api_ring_frame(int index, u2p_ring_frame_t *f)
{
    static TrxFrame tf;                         // 260 bytes: off the stack
    if (!trx_ring_frame(index, &tf)) {
        return U2P_ERR_NOT_FOUND;
    }
    memset(f, 0, sizeof(*f));
    f->seq = tf.seq;
    f->c24 = tf.c24;
    f->cycles = tf.cycles;
    f->line = tf.line;
    f->x = tf.x;
    f->flags = tf.flags;
    f->target = trx_ring_reachable(index);
    return U2P_OK;
}

static int api_ring_land(int index) { return trx_ring_land(index, NULL); }

static_assert(sizeof(u2p_trace_rec_t) == sizeof(TrxTraceRec), "trace record layout");
static_assert(sizeof(u2p_trace_hit_t) == sizeof(TrxTraceHit), "hit layout");

static int api_trace_status(u2p_trace_status_t *st)
{
    TrxTraceStatus ts;
    bool ok = trx_ring_trace_status(&ts);
    memset(st, 0, sizeof(*st));
    st->present = ts.present;
    st->on = ts.on;
    st->gap = ts.gap;
    st->records = ts.records;
    st->capacity = ts.capacity;
    st->lost = ts.lost;
    st->oldest_cycle = ts.oldest_cycle;
    st->newest_cycle = ts.newest_cycle;
    return ok ? U2P_OK : U2P_ERR_UNSUPPORTED;
}

static int api_trace_read(uint32_t first, uint32_t n, u2p_trace_rec_t *out) { return trx_ring_trace_read(first, n, (TrxTraceRec *)out); }

static int api_trace_whowrote(uint16_t addr, int limit, u2p_trace_hit_t *hits, int *exhausted)
{
    bool ex = true;
    int n = trx_ring_trace_whowrote(addr, limit, (TrxTraceHit *)hits, &ex);
    *exhausted = ex ? 1 : 0;                    // this application's ring has no unresolved-store count
    return n;
}

static int api_log_read(uint64_t *out, int max, uint32_t *lost) { return trx_ring_log_read(out, max, lost); }

static int api_video(u2p_video_t *v)
{
    memset(v, 0, sizeof(*v));
    v->frame_cycles = trx_ring_frame_cycles();
    v->fps_x100 = trx_ring_fps_x100();
    v->hz60 = trx_ring_fps() == 60;
    v->lines = v->hz60 ? 263 : 312;
    v->cycles_per_line = v->hz60 ? 65 : 63;
    v->cpu_hz = v->hz60 ? 1022727 : 985248;
    return U2P_OK;
}

static u2p_ring_api_t s_ring = {
    sizeof(u2p_ring_api_t), api_ring_status, api_ring_frame, trx_ring_find, api_ring_land, trx_ring_commit, trx_ring_error, { 0, 0, 0 },
    trx_ring_trace_ctl, api_trace_status, api_trace_read, 0, api_trace_whowrote, api_log_read,
};

static int api_stop_requested(void) { return 0; }

static u2p_trx_api_t s_trx = {
    sizeof(u2p_trx_api_t), (volatile uint8_t *)TRX_IO_BASE, 0, api_trx_acquire, api_trx_release, api_trx_announce, NULL, NULL, api_video, api_stop_requested, NULL,
};

// ---- what trxmon's main.c gave the engine (mach.h, rpc.h) ------------------------------------------------------------------------------------------------------
extern "C" {
uint32_t plat_ms(void) { return (uint32_t)xTaskGetTickCount() * portTICK_PERIOD_MS; }
void plat_yield(void) { taskYIELD(); }
void plat_sleep(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms < PLAT_SLEEP_MIN_MS ? PLAT_SLEEP_MIN_MS : ms)); }

// A file is held whole: a read loads it, a write is collected and saved when it is closed (CoreFs saves whole files; a medium it refuses to write says so at the close).
struct PlatFile {
    char path[200];
    bool write;
    uint8_t *buf;
    uint32_t len, pos, cap;
};

void *plat_fopen(const char *path, const char *mode)
{
    if (!g_c || !g_c->fs) {
        return 0;
    }
    PlatFile *f = (PlatFile *)calloc(1, sizeof(PlatFile));
    if (!f) {
        return 0;
    }
    snprintf(f->path, sizeof(f->path), "%s", path);
    f->write = mode[0] == 'w';
    if (!f->write) {
        uint64_t sz = 0;
        bool dir = false;
        if (!g_c->fs->stat(path, &sz, &dir) || dir || sz > 65536 + 2) {
            free(f);
            return 0;
        }
        f->buf = (uint8_t *)malloc(sz ? (size_t)sz : 1);
        int n = f->buf ? g_c->fs->load(path, f->buf, (uint32_t)sz) : -1;
        if (n < 0) {
            free(f->buf);
            free(f);
            return 0;
        }
        f->len = (uint32_t)n;
    }
    return f;
}

int plat_fread(void *buf, int n, void *fp)
{
    PlatFile *f = (PlatFile *)fp;
    int r = (int)(f->len - f->pos) < n ? (int)(f->len - f->pos) : n;
    memcpy(buf, f->buf + f->pos, r);
    f->pos += r;
    return r;
}

int plat_fwrite(const void *buf, int n, void *fp)
{
    PlatFile *f = (PlatFile *)fp;
    if (f->len + n > f->cap) {
        uint32_t cap = f->cap ? f->cap * 2 : 4096;
        while (cap < f->len + n) {
            cap *= 2;
        }
        uint8_t *b = (uint8_t *)realloc(f->buf, cap);
        if (!b) {
            return -1;
        }
        f->buf = b;
        f->cap = cap;
    }
    memcpy(f->buf + f->len, buf, n);
    f->len += n;
    return n;
}

void plat_fclose(void *fp)
{
    PlatFile *f = (PlatFile *)fp;
    if (f->write && g_c && g_c->fs && !g_c->fs->save(f->path, f->buf, f->len)) {
        printf("[monitor] %s: not written\n", f->path);
    }
    free(f->buf);
    free(f);
}

int plat_mkdir(const char *path) { return g_c && g_c->fs && g_c->fs->mkdir(path) ? 0 : -1; }

int plat_exists(const char *dir, const char *name)
{
    char p[FS_PATH_MAX];
    uint64_t sz;
    bool d;
    snprintf(p, sizeof(p), "%s/%s", dir, name);
    return g_c && g_c->fs && g_c->fs->stat(p, &sz, &d) ? 1 : 0;
}

int plat_dir(const char *path, plat_dir_cb cb, void *ctx)
{
    static FsEntryShort e[64];                         // the monitor prints the names: 55 characters of a FAT long name are enough there
    if (!g_c || !g_c->fs) {
        return -1;
    }
    int n = g_c->fs->list_short(path, e, 64);
    for (int i = 0; i < n; i++) {
        cb(ctx, e[i].name, (uint32_t)e[i].size, e[i].dir ? 1 : 0);
    }
    return n < 0 ? -1 : 0;
}

int plat_rmdir(const char *path) { return g_c && g_c->fs && g_c->fs->remove(path) ? 0 : -1; }

// The firmware's configuration items the verbs read: here the settings of this core (the display text of the row).
int plat_config(const char *, const char *item, char *buf, int len)
{
    char key[96];
    snprintf(key, sizeof(key), "set.%s", item);
    const char *v = g_c ? ops_state(g_c, key) : 0;
    if (!v) {
        return -1;
    }
    snprintf(buf, len, "%s", v);
    return (int)strlen(buf);
}

int plat_media(int, char *, int) { return 0; }
const char *plat_firmware_git(void) { return ""; }
const char *plat_firmware_version(void) { return ""; }
}

// ---- the session ---------------------------------------------------------------------------------------------------------------------------------------------
static ui_t s_ui;
static ui_screen_t s_scr;
static bool g_open;                             // a session stands (the engine is initialised, the debug block may be owned)
static bool g_ok;                               // the debug block is there
static bool g_leave, g_diag;
static bool g_menu;                             // the menu's monitor tab uses the session (an RPC client may use it too: monsrv_has_client())
static uint32_t g_hash, g_last;

static void session_open(CoreCtx *c)
{
    g_c = c;
    if (g_open) {
        return;
    }
    g_open = true;
    uint8_t caps = trx_io_caps();
    s_trx.caps = caps;
    TrxRingStatus rs;
    trx_ring_status(&rs);
    s_trx.ring = rs.present ? &s_ring : NULL;
    g_ok = (caps & TRX_CAPS_DBG) && hw_init(&s_trx);
    ui_init(&s_ui);
    ui_set_size(&s_ui, UI_WIDE_COLS, UI_WIDE_ROWS);
    int fkey[9] = { 0, KEY_PAGEUP, 0, KEY_HELP, 0, KEY_TASKS, 0, KEY_PAGEDOWN, 0 };       // the Ultimate interface: F1 F3 F5 F7 (ui_set_keys names them in the hint row)
    ui_set_keys(&s_ui, fkey);
    if (!g_ok) {
        ui_open(&s_ui);
        ui_print(&s_ui, "The debug block is not in this bitstream (CAPS b3): the monitor has nothing to talk to.", UA_ERR);
        return;
    }
    mach_init();
    mon_init();
    rpc_init();
    ui_open(&s_ui);
    rpc_take_user_stop();
    ui_print(&s_ui, "TRX64 Monitor (trxmon). F3 help, F1/F7 scroll, x resume+leave.", UA_NORMAL);
    if (monsrv_status()[0]) {
        ui_print(&s_ui, monsrv_status(), UA_NORMAL);
    }
}

void mon_open(CoreCtx *c)
{
    g_menu = true;
    session_open(c);
}

// An RPC client sent a command: the engine is needed now (monsrv.cc calls this before rpc_request). False: the debug block is not there.
extern "C" int mon_claim(void)
{
    if (!g_open && g_c) {
        session_open(g_c);
    }
    return g_open && g_ok;
}

static void session_end(void)
{
    if (g_ok) {
        api_trx_release();
    }
    g_open = false;
}

// Called every pass of the main loop, menu open or not (trxmon.u2a ran on a task of its own; the RPC answers whatever the menu does): listens on TCP 4312,
// answers what came, and ends the session again when neither the menu's tab nor a client uses it.
void mon_serve(CoreCtx *c)
{
    g_c = c;
    static uint32_t t_try;
    if (!monsrv_listening() && (!t_try || plat_ms() - t_try >= 5000)) {
        t_try = plat_ms() | 1;
        monsrv_start();
    }
    monsrv_poll();
    if (g_open && g_ok) {
        rpc_poll();
        if (rpc_quit_requested()) {                 // /quit over the RPC: the holder goes, as the application's end took it
            monsrv_drop_client();
            if (g_menu) {
                g_leave = true;
            }
        }
    }
    if (g_open && !g_menu && !monsrv_has_client()) {
        session_end();
    }
}

void mon_release(void)
{
    if (!g_open) {
        return;
    }
    g_menu = false;
    if (monsrv_has_client()) {                  // an RPC client still holds the session (breakpoints, observers)
        return;
    }
    session_end();
}

bool mon_wants_stop(void) { return g_open && g_ok && M.running; }

bool mon_key(CoreCtx *c, int k)
{
    g_menu = true;
    session_open(c);
    switch (k) {                                // the keyboards deliver F1 F3 F5 F7 as function keys; trxmon names them PAGEUP HELP TASKS PAGEDOWN, the monitor has those names here
    case KEY_F1: k = KEY_PAGEUP; break;
    case KEY_F3: k = KEY_HELP; break;
    case KEY_F5: k = KEY_TASKS; break;
    case KEY_F7: k = KEY_PAGEDOWN; break;
    default: break;
    }
    if (!g_ok) {
        return true;
    }
    if (k == KEY_RETURN && !strcmp(s_ui.cmd, "diag")) {               // the way to Diagnostics: no key of the monitor's leaves to another tab
        s_ui.clen = s_ui.cpos = 0;
        s_ui.cmd[0] = 0;
        g_diag = true;
        return true;
    }
    int m = ui_map_key(k);
    if (m) {
        int r = ui_key(&s_ui, m);
        if (r != UI_STAY) {
            g_leave = true;
        }
    }
    return true;
}

bool mon_leave_requested(void)
{
    bool r = g_leave;
    g_leave = false;
    return r;
}

bool mon_diag_requested(void)
{
    bool r = g_diag;
    g_diag = false;
    return r;
}

bool mon_tick(CoreCtx *c)
{
    if (!g_open || !g_ok) {
        return false;
    }
    g_c = c;
    rpc_poll();
    bool d = ui_tick(&s_ui) != 0;
    uint32_t now = plat_ms();
    if (now - g_last >= 250) {                  // the status rows follow the machine
        g_last = now;
        mon_render();
        uint32_t h = mon_sig();
        d |= h != g_hash;
    }
    return d;
}

void mon_run(CoreCtx *c, const char *line)
{
    g_menu = true;
    session_open(c);
    if (!g_ok) {
        return;
    }
    for (const char *p = line; *p; p++) {
        ui_key(&s_ui, (unsigned char)*p);
    }
    ui_key(&s_ui, UI_K_RETURN);
}

// ---- the rows ---------------------------------------------------------------------------------------------------------------------------------------------------
void mon_render(void)
{
    if (!g_open) {
        s_scr.cols = UI_WIDE_COLS;
        s_scr.rows = 0;
        return;
    }
    ui_render(&s_ui, &s_scr);
}

int mon_cols(void) { return s_scr.cols; }
int mon_rows(void) { return s_scr.rows; }
const char *mon_line(int r) { return r >= 0 && r < s_scr.rows ? s_scr.row[r] : ""; }
int mon_attr(int r) { return r >= 0 && r < s_scr.rows ? s_scr.attr[r] : 0; }
int mon_blank_rows(void) { return s_scr.blank_rows; }
void mon_cursor(int *x, int *y)
{
    *x = s_scr.cursor_x;
    *y = s_scr.cursor_y;
}

uint32_t mon_sig(void)
{
    uint32_t h = 2166136261u;
    for (int r = 0; r < s_scr.rows; r++) {
        for (const char *p = s_scr.row[r]; *p; p++) {
            h = (h ^ (uint8_t)*p) * 16777619u;
        }
        h = (h ^ (uint32_t)s_scr.attr[r] ^ 0x100u) * 16777619u;
    }
    h = (h ^ (uint32_t)s_scr.cursor_x ^ ((uint32_t)s_scr.cursor_y << 8)) * 16777619u;
    g_hash = h;
    return h;
}

// ---- operations (the Help node of the tab runs a command in the monitor) -------------------------------------------------------------------------------
static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    mon_run(c, a.s(0));
    snprintf(o.msg, o.msglen, "%s", a.s(0));
    return OPE_OK;
}
static const ArgSpec line_arg[] = { ARG_STRING("line", 60) };

// T192.3 leak 2: the monitor's keys are an operation, not a branch of the menu controller. The key table (menukeytab.cc) sends every key of the monitor's screen to it; the answer
// is what the controller does next: "goto F8" (the line `diag`: the System tab, F8; the session is released first), "close" (`x`: resume and leave), else nothing.
static int op_key(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    mon_key(c, a.i(0));
    o.msg[0] = 0;
    if (mon_diag_requested()) {
        mon_release();
        snprintf(o.msg, o.msglen, "goto F8");
    } else if (mon_leave_requested()) {
        snprintf(o.msg, o.msglen, "close");
    }
    return OPE_OK;
}
static const ArgSpec key_arg[] = { ARG_INT("key", 0, 0x1FFF) };
// The pump's look at the monitor while its screen is shown (stops, finished run-tos, the live log): `more` says the screen changed.
static int op_tick(CoreCtx *c, const OpArgs &, OpOut &o)
{
    o.msg[0] = 0;
    o.more = mon_tick(c);
    return OPE_OK;
}
static const char *st_wants_stop(CoreCtx *) { return mon_wants_stop() ? "yes" : 0; }
static int op_open(CoreCtx *c, const OpArgs &, OpOut &o)
{
    o.msg[0] = 0;
    mon_open(c);                                   // the monitor tab is shown: its session starts (once, until mon_release())
    return OPE_OK;
}

void monitor_register(OpRegistry &r)
{
    r.contributor("monitor");
    r.add_op("mon.run", line_arg, 1, op_run);
    r.add_op("monitor.key", key_arg, 1, op_key);
    r.add_op("monitor.open", 0, 0, op_open);
    r.add_op("monitor.tick", 0, 0, op_tick);
    r.add_state("monitor.wants_stop", st_wants_stop);
}
