// The "Port Diagnostics" descriptor (T175): a core without a machine - the probe engine, its registers and this application. The idle page is the live
// state of the ten joystick-port pins and the four comparator bits (the home page's media rows, read from the register at every redraw); a test is a .toml
// of /SD/cores/diag/tests, picked in the browser or from the task, run on a task of its own, and leaves <test>_<date>.toml and .txt in results/ (diagtest.h).
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "portdiag_regs.h"
#include "portdiag.h"
#include "diagtest.h"
#include "panel.h"
#include "corefolder.h"
#include "picture.h"
#include "ops_glue.h"
#include "xinput.h"
#include <stdio.h>

static CoreCtx *g_ctx;
static CoreDialogs *host() { return g_ctx ? g_ctx->dialogs : 0; }

// ---- the live page ---------------------------------------------------------------------------------------------------------------------------
static char g_p1[40], g_p2[40], g_cmp[24];

static const char *port_text(CoreCtx *c, int port, char *buf, int n)
{
    uint32_t live = diag_live(c->io);
    static const char dirs[5] = { 'U', 'D', 'L', 'R', 'F' };
    int o = 0;
    for (int b = 0; b < 5; b++) {
        o += snprintf(buf + o, n - o, "%s%c%d", b ? "  " : "", dirs[b], (int)((live >> (port * 5 + b)) & 1));
    }
    return buf;
}
static const char *st_p1(CoreCtx *c) { return port_text(c, 0, g_p1, sizeof(g_p1)); }
static const char *st_p2(CoreCtx *c) { return port_text(c, 1, g_p2, sizeof(g_p2)); }
static const char *st_cmp(CoreCtx *c)
{
    uint32_t live = diag_live(c->io);
    snprintf(g_cmp, sizeof(g_cmp), "%d  %d  %d  %d", (int)((live >> 10) & 1), (int)((live >> 11) & 1), (int)((live >> 12) & 1), (int)((live >> 13) & 1));
    return g_cmp;
}

static char g_last[120] = "No test run yet";
static bool g_busy;
static const char *st_last(CoreCtx *) { return g_last; }

// ---- the registers by name, the panels, the A/V walk's hooks --------------------------------------------------------------------------------------------------
static char g_folder[200];
static void folder_of(CoreCtx *c)
{
    if (!corefolder_dir(c->cor_path, g_folder, sizeof(g_folder))) {
        snprintf(g_folder, sizeof(g_folder), "/SD/cores/diag");
    }
}

// The A/V panel file's rows are the knobs of the A/V walks (diagtest.cc). It is read from <folder>/panels/avout.toml when a walk starts.
static Panel g_avp;
static char g_avp_text[8192];
static const Panel *av_panel_cb(void *ud, char *why, int n)
{
    CoreCtx *c = (CoreCtx *)ud;
    char path[260];
    folder_of(c);
    snprintf(path, sizeof(path), "%s/panels/avout.toml", g_folder);
    int len = c->fs->load(path, g_avp_text, sizeof(g_avp_text) - 1);
    if (len < 0) {
        snprintf(why, n, "the A/V panel %s is not on the card", path);
        return 0;
    }
    g_avp_text[len] = 0;
    char e[120];
    if (panel_parse(g_avp_text, (uint32_t)len, &g_avp, portdiag_reg_by_name, e, sizeof(e))) {
        snprintf(why, n, "%s: %s", path, e);
        return 0;
    }
    return &g_avp;
}
// What the walk reads and writes: the knob's label index (the register's value, its default when the register holds a value without a name), a knob into the register, and the panel's values back
// after the walk (the rows as they were when the walk first asked).
static int32_t g_av_keep[PNL_ROWS];
static bool g_av_kept;
static void av_keep(CoreCtx *c)
{
    if (!g_av_kept) {
        for (int k = 0; k < g_avp.nrows; k++) {
            g_av_keep[k] = g_avp.row[k].action[0] ? 0 : panel_get(c, g_avp.row[k]);
        }
        g_av_kept = true;
    }
}
static int av_get_cb(void *ud, int k)
{
    CoreCtx *c = (CoreCtx *)ud;
    av_keep(c);
    int i = panel_name_idx(g_avp.row[k], panel_get(c, g_avp.row[k]));
    if (i < 0) {
        i = panel_name_idx(g_avp.row[k], g_avp.row[k].def);
    }
    return i < 0 ? 0 : i;
}
static void av_set_cb(void *ud, int k, int idx)
{
    CoreCtx *c = (CoreCtx *)ud;
    av_keep(c);
    if (idx >= 0 && idx < g_avp.row[k].n) {
        panel_put(c, g_avp.row[k], g_avp.row[k].vals[idx]);
    }
}
static void av_freeze_cb(void *ud, bool on)
{
    PanelReg g;
    CoreCtx *c = (CoreCtx *)ud;
    if (portdiag_reg_by_name("av.vinv.freeze", &g)) {                  // the marker stands still while a question is open: bit 1 of 053, a write keeps the other bit
        uint8_t v = c->io->rd(g.ofs);
        c->io->wr(g.ofs, (uint8_t)((v & ~(1 << g.shift)) | (on ? 1 << g.shift : 0)));
    }
}
static void av_done_cb(void *ud)
{
    CoreCtx *c = (CoreCtx *)ud;
    av_freeze_cb(ud, false);
    if (g_av_kept) {
        for (int k = 0; k < g_avp.nrows; k++) {
            if (!g_avp.row[k].action[0]) {
                panel_put(c, g_avp.row[k], g_av_keep[k]);
            }
        }
        g_av_kept = false;
    }
}

// ---- the run ---------------------------------------------------------------------------------------------------------------------------------
static uint32_t env_now(void) { return g_ctx->io->now_ms(); }
static void env_delay(uint32_t ms) { g_ctx->io->delay_ms(ms); }
static bool env_stamp(char out[16]) { return host() && host()->stamp(out); }
static void env_joy_menu(void *, bool on) { if (host()) { host()->joystick_menu(on); } }

// T196 S7: the XInput pad test (diagtest.cc run_xpadtest) sets the driver's poll interval and ack switch and reads its counters (xinput.h XiStats)
static uint32_t g_xp_cb, g_xp_ms;
static void xpad_begin_cb(void *, int poll_ms, bool ack_input)
{
    xi_cfg.poll_ms = poll_ms;
    xi_cfg.ack_input = ack_input;
    xi_stats_reset();
    g_xp_cb = 0;
    g_xp_ms = g_ctx->io->now_ms();
}
static void xpad_reset_cb(void *)
{
    xi_stats_reset();
    g_xp_cb = 0;
    g_xp_ms = g_ctx->io->now_ms();
}
static int xpad_text_cb(void *, char *out, int cap)
{
    uint32_t now = g_ctx->io->now_ms();
    uint32_t cb = xi_stats.cb;
    uint32_t rate = now - g_xp_ms >= 500 ? (uint32_t)(((uint64_t)(cb - g_xp_cb) * 1000 + (now - g_xp_ms) / 2) / (now - g_xp_ms)) : 0;
    if (now - g_xp_ms >= 1000) {                       // the rate is over the last second or so
        g_xp_cb = cb;
        g_xp_ms = now;
    }
    return xi_stats_text(out, cap, rate);
}
static void xpad_end_cb(void *)
{
    xi_cfg.poll_ms = 0;                                // production: the pad that is plugged in later is polled at its own bInterval
    xi_cfg.ack_input = true;
}

void portdiag_prompt_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn)
{
    snprintf(t, tn, "%s  (%d of %d)", test, idx, total);
    snprintf(x, xn, "%s\n\nYES: done, measure.   NO: skip this step.   ESC: stop the test.", text);
}

void portdiag_question_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn)
{
    snprintf(t, tn, "%s  (%d of %d)", test, idx, total);
    snprintf(x, xn, "%s\n\nYES or NO.   ESC: stop the test.", text);
}

class PdUi : public DiagUi {
public:
    int prompt(const char *title, const char *text, int idx, int total, int timeout_s) override
    {
        char t[96], x[400];
        portdiag_prompt_text(title, text, idx, total, t, sizeof(t), x, sizeof(x));
        int a = host()->ask(t, x, timeout_s);
        return a > 0 ? DUI_CONTINUE : a == 0 ? DUI_SKIP : a == -2 ? DUI_TIMEOUT : DUI_ABORT;
    }
    int question(const char *title, const char *text, int idx, int total, int timeout_s) override
    {
        char t[128], x[1700];                         // an xpad test's counters are about 700 characters
        portdiag_question_text(title, text, idx, total, t, sizeof(t), x, sizeof(x));
        int a = host()->ask(t, x, timeout_s);
        return a > 0 ? DUI_CONTINUE : a == 0 ? DUI_SKIP : a == -2 ? DUI_TIMEOUT : DUI_ABORT;
    }
    int question_live(const char *title, const char *text, void (*live)(void *), void *ud) override
    {
        const char *t = title;                         // the live test is one screen: no "1 of 1"; its own text carries the keys
        int a = host()->ask_live(t, text, live, ud);
        return a > 0 ? DUI_CONTINUE : a == 0 ? DUI_SKIP : DUI_ABORT;
    }
    int choice(const char *title, const char *text, int idx, int total, int timeout_s, const char *const *items, int n, int *pick) override
    {
        char t[96], x[400];
        snprintf(t, sizeof(t), "%s  (%d of %d)", title, idx, total);
        snprintf(x, sizeof(x), "%s", text);
        int a = host()->choose(t, x, items, n, timeout_s);
        if (a >= 0 && a < n) {
            *pick = a;
            return DUI_CONTINUE;
        }
        return a == -2 ? DUI_TIMEOUT : DUI_ABORT;
    }
    void progress(const char *) override { }
    void show(const char *title, const char *text) override { host()->notice(title, text); }
};

static char g_path[260];

// A file picked or named is a test (kinds test, suite, avtest, xpad: diagtest.cc) or a panel (kind = "panel"): the first line `kind = ` says which.
static bool is_panel_text(const char *t, int n)
{
    const char *p = t, *e = t + n;
    while (p < e) {
        while (p < e && (*p == ' ' || *p == '\t')) {
            p++;
        }
        if (!strncmp(p, "kind", 4) && (p[4] == ' ' || p[4] == '=')) {
            const char *q = (const char *)memchr(p, '"', (size_t)(e - p));
            return q && (size_t)(e - q) >= 7 && !strncmp(q, "\"panel\"", 7);
        }
        if (*p == '[') {
            return false;
        }
        const char *nl = (const char *)memchr(p, '\n', (size_t)(e - p));
        p = nl ? nl + 1 : e;
    }
    return false;
}

static Panel g_panel;
static char g_panel_text[8192];
static bool panel_path(CoreCtx *c, const char *path)
{
    int n = c->fs->load(path, g_panel_text, sizeof(g_panel_text) - 1);
    if (n < 0) {
        snprintf(g_last, sizeof(g_last), "not opened: cannot read %s", path);
        host()->notice("Port Diagnostics", g_last);
        return true;
    }
    g_panel_text[n] = 0;
    if (!is_panel_text(g_panel_text, n)) {
        return false;
    }
    char e[160];
    if (panel_parse(g_panel_text, (uint32_t)n, &g_panel, portdiag_reg_by_name, e, sizeof(e))) {
        const char *fn = strrchr(path, '/');
        snprintf(g_last, sizeof(g_last), "not opened: %s", e);
        char m[260];
        snprintf(m, sizeof(m), "%s: %s", fn ? fn + 1 : path, e);
        host()->notice("Port Diagnostics", m);
        return true;
    }
    const char *fn = strrchr(path, '/');
    snprintf(g_last, sizeof(g_last), "panel %s", fn ? fn + 1 : path);
    panel_run(c, host(), g_panel, g_folder);
    return true;
}

static void run_path(CoreCtx *c, const char *path)
{
    static PdUi ui;
    static DiagOutcome out;
    if (panel_path(c, path)) {
        return;
    }
    DiagEnv env;
    memset(&env, 0, sizeof(env));
    env.io = c->io;
    env.fs = c->fs;
    env.ui = &ui;
    env.now_ms = env_now;
    env.delay_ms = env_delay;
    env.stamp = env_stamp;
    env.core_version = c->core_version;
    env.folder = g_folder;
    env.av_panel = av_panel_cb;
    env.av_get = av_get_cb;
    env.av_set = av_set_cb;
    env.av_done = av_done_cb;
    env.av_freeze = av_freeze_cb;
    env.av_ud = c;
    env.joy_menu = env_joy_menu;
    env.xpad_begin = xpad_begin_cb;
    env.xpad_reset = xpad_reset_cb;
    env.xpad_text = xpad_text_cb;
    env.xpad_end = xpad_end_cb;
    const char *fn = strrchr(path, '/');
    snprintf(g_last, sizeof(g_last), "running %s", fn ? fn + 1 : path);
    int rc = diag_run_file(&env, path, &out);
    if (rc) {
        snprintf(g_last, sizeof(g_last), "not run: %s", out.msg);
        host()->notice("Port Diagnostics", out.msg);
    } else {
        snprintf(g_last, sizeof(g_last), "%s: %d pass, %d fail, %d refused, %d error", fn ? fn + 1 : path, out.npass, out.nfail, out.nrefused, out.nerror);
    }
}

static const char *g_pick_sub = "";                    // the folder under the core's folder the task's picker starts in ("" = the core's folder, which holds tests, panels, presets, results)
static void worker(void *arg)
{
    CoreCtx *c = (CoreCtx *)arg;
    g_ctx = c;
    if (!g_path[0]) {                                  // the task's own picker
        char start[220];
        snprintf(start, sizeof(start), "%s%s%s", g_folder, g_pick_sub[0] ? "/" : "", g_pick_sub);
        if (!host()->pick(start, g_path, sizeof(g_path))) {
            snprintf(g_last, sizeof(g_last), "nothing picked");
            g_busy = false;
            return;
        }
    }
    run_path(c, g_path);
    g_path[0] = 0;
    g_busy = false;
}

static int start(CoreCtx *c, const char *path, const char *sub, OpOut &o)
{
    g_ctx = c;
    if (!host()) {
        snprintf(o.msg, o.msglen, "This build has no dialogs for the runner");
        return OPE_UNSUPPORTED;
    }
    if (g_busy) {
        snprintf(o.msg, o.msglen, "A test or a panel is open");
        return OPE_BUSY;
    }
    folder_of(c);
    g_ctx = c;
    g_pick_sub = sub;
    snprintf(g_path, sizeof(g_path), "%s", path);
    g_busy = true;
    if (!host()->spawn(worker, c)) {
        g_busy = false;
        snprintf(o.msg, o.msglen, "The runner's task did not start");
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Started");
    return OPE_OK;
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o) { return start(c, a.s(0), "", o); }
static int op_pick(CoreCtx *c, const OpArgs &, OpOut &o) { return start(c, "", "tests", o); }
static int op_panel(CoreCtx *c, const OpArgs &, OpOut &o) { return start(c, "", "panels", o); }
// ---- the start -------------------------------------------------------------------------------------------------------------------------------
static int pd_init(CoreCtx *c, char *, int)
{
    c->io->wr(PD_CTRL, PD_CTRL_ABORT);                 // every output released
    c->io->wr(PD_CTRL, PD_CTRL_CLEAR);
    folder_of(c);
    g_ctx = c;
    panel_set_system_hook(picture_set_system);          // a panel row with `system` (the Standard of the A/V panel) sets the System Mode as the C128's page does
    return 0;
}

static int pd_release(CoreCtx *c, char *, int)
{
    c->io->wr(PD_CTRL, PD_CTRL_ABORT);
    return 0;
}

static const OpBind run_bind[] = { B_FILE("path") };
static const FileAction toml_actions[] = {
    { "Run test or open panel", "pd.run", run_bind, 1, false, 0, 0, 0, 0, 'R' },
};
static const char *const pd_caps[] = { "pad" };
static const FileType filetypes[] = {
    { ".toml", toml_actions, 1, 0, 0, 0 },
};
static const Task tasks[] = {
    { "Pick a test and run it", "pd.pick", 0, 0, false },
    { "Open a panel", "pd.panel", 0, 0, false },
};
static const MediaSlot media[] = {
    { "Port 1", "pd.port1", 0, 0, 0, 0 },
    { "Port 2", "pd.port2", 0, 0, 0, 0 },
    { "Comparators", "pd.cmp", 0, 0, 0, 0 },
    { "Last test", "pd.last", 0, 0, 0, 0 },
};

static const MemMap pd_mem = { 0, 0, false, "The diag core has no memory the application reaches" };

static void pd_register(OpRegistry &r)
{
    r.contributor("portdiag");
    static const ArgSpec run_args[] = { ARG_PATH("path") };
    r.add_op("pd.run", run_args, 1, op_run);
    r.add_op("pd.pick", 0, 0, op_pick);
    r.add_op("pd.panel", 0, 0, op_panel);
    r.add_state("pd.port1", st_p1);
    r.add_state("pd.port2", st_p2);
    r.add_state("pd.cmp", st_cmp);
    r.add_state("pd.last", st_last);
    MEM_OPS(r, pd_mem, 0xFFFF);
}

static const ReportReg report_regs[] = {
    { "caps", PD_CAPS, 1 }, { "status", PD_STATUS, 1 }, { "pc", PD_PC, 2 }, { "fifo level", PD_FIFO_LEVEL, 2 }, { "ticks per us x256", PD_TICKS, 4 }, { "live pins", PD_LIVE, 2 },
};

extern const CoreDesc portdiag_desc;
const CoreDesc portdiag_desc = {
    { 'D', 'I', 'A', 'G' },
    "Port Diagnostics",
    "Port Diagnostics",
    "DIAG",
    "diag",
    "Port Diagnostics Settings",
    1,
    0, 0,
    0, 0,                                                  // no settings: the A/V rows are a panel (panels/avout.toml)
    tasks, 2,
    filetypes, 1,
    media, 4,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },              // no picture of its own: the application's screens are all there is
    { 0, 0, 200 },                                         // nothing to stop: Freeze does not wait
    pd_init, pd_release, 0, 20,
    0, 0, false,
    0, 0,
    pd_register,
    pd_caps, 1,                                            // caps: `pad` only lets the XInput driver claim a pad for the xpad test (T196 S7); the core has no PadSpec
    false,                                                 // board_caps
    false, 0, 0, 0, 0, 0, 0, 0,                            // id_shared, reset_writes, romset, window_media, sector_media, mouse, fbwin
    0, 0,                                                  // type_op, run_op
    0, 0, 0, 0, 0, 0, 0,                                   // cold_ram_bytes, cold_start, id_feat (3), menu_rec, menu_rec_n
    "diag",                                                // core_id
    report_regs, (int)(sizeof(report_regs) / sizeof(report_regs[0])),
    0,                                                     // rawkeys
    false,                                                 // own_volume
    0,                                                     // pad
    250, true,                                             // live_ms, menu_first
};
