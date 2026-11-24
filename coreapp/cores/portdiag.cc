// The "Port Diagnostics" descriptor (T175): a core without a machine - the probe engine, its registers and this application. The idle page is the live
// state of the ten joystick-port pins and the four comparator bits (the home page's media rows, read from the register at every redraw); a test is a .toml
// of /SD/cores/diag/tests, picked in the browser or from the task, run on a task of its own, and leaves <test>_<date>.toml and .txt in results/ (diagtest.h).
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "portdiag_regs.h"
#include "portdiag.h"
#include "diagtest.h"
#include "instfiles.h"
#include "corefolder.h"
#include "picture.h"
#include "settings_ops.h"
#include "ops_glue.h"
#include "xinput.h"
#include <stdio.h>

static PdHost *g_host;
void portdiag_attach(PdHost *h) { g_host = h; }

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

// ---- the A/V page (T201): one row per knob of the test picture's encoder (gateware cores/diag/rtl/diag_avout.v), live ----------------------------------------------
// The rows are the descriptor's settings (the "Port Diagnostics Settings" tab), built from the knob table of the test runner (diagtest.cc) so a test file and the page name the same values.
// A row writes its register at once; its value stays in the core's .cfg like any setting. The standard also sets the System Mode (the clock the subcarrier constant is for), as the C128's does.
static const uint8_t av_sysfmt[4] = { 0x00, 0x2B, 0x2A, 0x01 };            // C64_VIDEOFORMAT per mode, as cores/c128.cc sysmode_fmt

// T259 P6: a knob takes any value of its register range (the playground), not only a label's; g_av_raw is what each knob holds now, what Save writes.
static int16_t g_av_raw[DT_AV_N];
static void av_put_raw(CoreCtx *c, int k, int v)
{
    const AvKnob &kn = dt_av_knobs[k];
    v = v < kn.lo ? kn.lo : v > kn.hi ? kn.hi : v;
    g_av_raw[k] = (int16_t)v;
    CoreIo *io = c->io;
    switch (k) {
    case AVK_STD:
        io->wr(PD_AV_STD, (uint8_t)v);
        picture_set_system(c, v);
        io->video_format(av_sysfmt[v & 3]);
        break;
    case AVK_GROUPS: io->wr(PD_AV_PERM, (uint8_t)v); break;
    case AVK_BITS_A: case AVK_BITS_B: case AVK_BITS_C: {
        int bit = k - AVK_BITS_A;
        uint8_t r = (uint8_t)((io->rd(PD_AV_REV) & ~(1 << bit)) | (v << bit));
        io->wr(PD_AV_REV, r);
        break;
    }
    case AVK_VSIGN: io->wr(PD_AV_VINV, (uint8_t)((io->rd(PD_AV_VINV) & ~1) | (v & 1))); break;      // 053 is two bits: V sign, freeze - a write keeps the other one
    case AVK_TRIM: io->wr(PD_AV_TRIM, (uint8_t)(int8_t)v); break;
    case AVK_BSTART: io->wr(PD_AV_BSTART, (uint8_t)v); break;
    case AVK_BEND: io->wr(PD_AV_BEND, (uint8_t)(v & 0xFF)); io->wr(PD_AV_BEND + 1, (uint8_t)(v >> 8)); break;
    case AVK_BAMP: io->wr(PD_AV_BAMP, (uint8_t)v); break;
    case AVK_GAIN: io->wr(PD_AV_GAIN, (uint8_t)v); break;
    case AVK_SYNC: io->wr(PD_AV_SYNC, (uint8_t)v); break;
    case AVK_BLANK: io->wr(PD_AV_BLANK, (uint8_t)v); break;
    case AVK_WHITE: io->wr(PD_AV_WHITE, (uint8_t)v); break;
    case AVK_EQ: io->wr(PD_AV_SYNCB, (uint8_t)((io->rd(PD_AV_SYNCB) & ~1) | (v & 1))); break;          // 05D is two bits: equalizing, 262 lines - a write keeps the other one
    case AVK_VLEN: io->wr(PD_AV_VLEN, (uint8_t)v); break;
    case AVK_VPOS: io->wr(PD_AV_VPOS, (uint8_t)(int8_t)v); break;
    case AVK_LINES: io->wr(PD_AV_SYNCB, (uint8_t)((io->rd(PD_AV_SYNCB) & ~2) | ((v & 1) << 1))); break;
    default: io->wr(PD_AV_MAP + (k - AVK_MAP), (uint8_t)v); break;                  // Map, Mode, Probe channel, RGB picture, SYNCn, FB: 060-065
    }
}
static void av_put(CoreCtx *c, int k, int idx)
{
    if (idx >= 0 && idx < dt_av_knobs[k].n) {
        av_put_raw(c, k, dt_av_knobs[k].val[idx]);
    }
}

template <int K> static int op_av(CoreCtx *c, const OpArgs &a, OpOut &)
{
    av_put(c, K, a.i(0));
    return OPE_OK;
}
static int (*const av_ops[DT_AV_N])(CoreCtx *, const OpArgs &, OpOut &) = {
    op_av<0>, op_av<1>, op_av<2>, op_av<3>, op_av<4>, op_av<5>, op_av<6>, op_av<7>, op_av<8>, op_av<9>, op_av<10>, op_av<11>, op_av<12>, op_av<13>, op_av<14>, op_av<15>, op_av<16>, op_av<17>, op_av<18>, op_av<19>, op_av<20>, op_av<21>, op_av<22>, op_av<23>
};
static const char *const av_op_names[DT_AV_N] = {
    "pd.av0", "pd.av1", "pd.av2", "pd.av3", "pd.av4", "pd.av5", "pd.av6", "pd.av7", "pd.av8", "pd.av9", "pd.av10", "pd.av11", "pd.av12", "pd.av13", "pd.av14", "pd.av15", "pd.av16", "pd.av17", "pd.av18", "pd.av19", "pd.av20", "pd.av21", "pd.av22", "pd.av23"
};

static Setting g_av_settings[DT_AV_N];
static ArgSpec g_av_args[DT_AV_N][1];
static struct AvInit {
    AvInit()
    {
        for (int k = 0; k < DT_AV_N; k++) {
            Setting &s = g_av_settings[k];
            memset(&s, 0, sizeof(s));
            s.key = dt_av_knobs[k].key;
            s.values = dt_av_knobs[k].labels;
            s.nvalues = dt_av_knobs[k].n;
            s.def = dt_av_knobs[k].def;
            g_av_raw[k] = dt_av_knobs[k].val[s.def];
            s.when = WHEN_LIVE;
            s.apply = av_op_names[k];
            ArgSpec a = ARG_INT("value", 0, dt_av_knobs[k].n - 1);
            g_av_args[k][0] = a;
        }
    }
} g_av_init;

// What the test runner reads and writes (diagtest.h DiagEnv): the page's stored index of a knob, a knob straight into the gateware, and the page's own values back.
static int av_page_idx(CoreCtx *c, int k)
{
    if (c->settings) {
        int i = c->settings->find(dt_av_knobs[k].key);
        if (i >= 0) {
            return c->settings->value(i);
        }
    }
    return dt_av_knobs[k].def;
}
static int av_get_cb(void *ud, int k) { return av_page_idx((CoreCtx *)ud, k); }
static void av_set_cb(void *ud, int k, int idx) { av_put((CoreCtx *)ud, k, idx); }
static void av_freeze_cb(void *ud, bool on)
{
    CoreIo *io = ((CoreCtx *)ud)->io;
    io->wr(PD_AV_VINV, (uint8_t)((io->rd(PD_AV_VINV) & ~2) | (on ? 2 : 0)));
}
static void av_done_cb(void *ud)
{
    CoreCtx *c = (CoreCtx *)ud;
    av_freeze_cb(ud, false);
    for (int k = 0; k < DT_AV_N; k++) {
        av_put(c, k, av_page_idx(c, k));
    }
}

// ---- the run ---------------------------------------------------------------------------------------------------------------------------------
static CoreCtx *g_ctx;
static uint32_t env_now(void) { return g_ctx->io->now_ms(); }
static void env_delay(uint32_t ms) { g_ctx->io->delay_ms(ms); }
static bool env_stamp(char out[16]) { return g_host && g_host->stamp(out); }
static void env_joy_menu(void *, bool on) { if (g_host) { g_host->joystick_menu(on); } }

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
        int a = g_host->ask(t, x, timeout_s);
        return a > 0 ? DUI_CONTINUE : a == 0 ? DUI_SKIP : a == -2 ? DUI_TIMEOUT : DUI_ABORT;
    }
    int question(const char *title, const char *text, int idx, int total, int timeout_s) override
    {
        char t[128], x[1700];                         // an xpad test's counters are about 700 characters
        portdiag_question_text(title, text, idx, total, t, sizeof(t), x, sizeof(x));
        int a = g_host->ask(t, x, timeout_s);
        return a > 0 ? DUI_CONTINUE : a == 0 ? DUI_SKIP : a == -2 ? DUI_TIMEOUT : DUI_ABORT;
    }
    int question_live(const char *title, const char *text, void (*live)(void *), void *ud) override
    {
        const char *t = title;                         // the live test is one screen: no "1 of 1"; its own text carries the keys
        int a = g_host->ask_live(t, text, live, ud);
        return a > 0 ? DUI_CONTINUE : a == 0 ? DUI_SKIP : DUI_ABORT;
    }
    int choice(const char *title, const char *text, int idx, int total, int timeout_s, const char *const *items, int n, int *pick) override
    {
        char t[96], x[400];
        snprintf(t, sizeof(t), "%s  (%d of %d)", title, idx, total);
        snprintf(x, sizeof(x), "%s", text);
        int a = g_host->choose(t, x, items, n, timeout_s);
        if (a >= 0 && a < n) {
            *pick = a;
            return DUI_CONTINUE;
        }
        return a == -2 ? DUI_TIMEOUT : DUI_ABORT;
    }
    void progress(const char *) override { }
    void show(const char *title, const char *text) override { g_host->notice(title, text); }
};

static char g_folder[200];
static char g_path[260];

static void folder_of(CoreCtx *c)
{
    if (!corefolder_dir(c->cor_path, g_folder, sizeof(g_folder))) {
        snprintf(g_folder, sizeof(g_folder), "/SD/cores/diag");
    }
}

// The presets: <folder>/presets/<name>.toml, the page's values in user terms. Saving writes the 14 rows; loading sets each row through settings.set (register and .cfg).
static bool preset_name_ok(const char *n)
{
    if (!*n || strlen(n) > 40) {
        return false;
    }
    for (; *n; n++) {
        if (*n == '/' || *n == '\\' || *n == '"' || *n == ':' || (unsigned char)*n < 0x20) {
            return false;
        }
    }
    return true;
}
static int op_av_save(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *name = a.s(0);
    if (!preset_name_ok(name)) {
        snprintf(o.msg, o.msglen, "The name is empty, longer than 40 or has / \\ : or a quote");
        return OPE_ARG;
    }
    folder_of(c);
    static char buf[1600];
    int n = snprintf(buf, sizeof(buf), "# TRX64U Port Diagnostics A/V preset (T201, T259): the rows of the A/V page in user terms, \"#n\" a free register value. Load it from the file browser (Load A/V preset) or by name.\nkind = \"avpreset\"\n\n[preset]\nname = \"%s\"\n", name);
    for (int k = 0; k < DT_AV_N && n < (int)sizeof(buf) - 80; k++) {
        int i = dt_av_idx_of_raw(k, g_av_raw[k]);
        n += i >= 0 ? snprintf(buf + n, sizeof(buf) - n, "%s = \"%s\"\n", dt_av_knobs[k].key, dt_av_knobs[k].labels[i]) : snprintf(buf + n, sizeof(buf) - n, "%s = \"#%d\"\n", dt_av_knobs[k].key, (int)g_av_raw[k]);
    }
    char dir[240], path[300];
    snprintf(dir, sizeof(dir), "%s/presets", g_folder);
    c->fs->mkdir(g_folder);
    if (!c->fs->mkdir(dir)) {
        snprintf(o.msg, o.msglen, "Cannot make %s (read only card?)", dir);
        return OPE_FAILED;
    }
    snprintf(path, sizeof(path), "%s/%s.toml", dir, name);
    if (!c->fs->save(path, buf, (uint32_t)n)) {
        snprintf(o.msg, o.msglen, "Cannot write %s (read only card?)", path);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Preset written to %s", path);
    return OPE_OK;
}
static int av_load_path(CoreCtx *c, const char *path, OpOut &o)
{
    static char buf[2048];
    int n = c->fs->load(path, buf, sizeof(buf) - 1);
    if (n < 0) {
        snprintf(o.msg, o.msglen, "Cannot read %s", path);
        return OPE_FAILED;
    }
    buf[n] = 0;
    if (!strstr(buf, "kind = \"avpreset\"")) {
        snprintf(o.msg, o.msglen, "Not an A/V preset (a test file runs with Run test)");
        return OPE_ARG;
    }
    int32_t raw[DT_AV_N];
    dt_av_preset_parse(buf, raw);
    int done = 0, bad = 0;
    for (int k = 0; k < DT_AV_N; k++) {                      // standard first (it is the first row): the others do not depend on it; a row the file lacks keeps its value
        int i = raw[k] == DT_AV_ABSENT || raw[k] == DT_AV_BADVAL ? -1 : dt_av_idx_of_raw(k, raw[k]);
        OpResult r;
        bad += raw[k] == DT_AV_BADVAL;
        if (i >= 0) {
            done += settings_set(c, dt_av_knobs[k].key, dt_av_knobs[k].labels[i], &r) == OPE_OK;
        } else if (raw[k] != DT_AV_ABSENT && raw[k] != DT_AV_BADVAL) {
            av_put_raw(c, k, raw[k]);                        // a free value: into the register, the page's row keeps its label
            done++;
        }
    }
    snprintf(o.msg, o.msglen, "%d rows loaded, %d unknown", done, bad);
    return bad ? OPE_FAILED : OPE_OK;
}
static int op_av_load(CoreCtx *c, const OpArgs &a, OpOut &o) { return av_load_path(c, a.s(0), o); }
static int op_av_preset(CoreCtx *c, const OpArgs &a, OpOut &o)       // T259 P4: a preset by its name, from <folder>/presets
{
    char path[300];
    if (!preset_name_ok(a.s(0))) {
        snprintf(o.msg, o.msglen, "The name is empty, longer than 40 or has / \\ : or a quote");
        return OPE_ARG;
    }
    folder_of(c);
    snprintf(path, sizeof(path), "%s/presets/%s.toml", g_folder, a.s(0));
    return av_load_path(c, path, o);
}
// T259 P6, the playground: "knob=value" sets a knob to any value of its register range (a number), or to one of its labels; the page's row keeps the label it had
static int op_av_free(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    char t[64];
    snprintf(t, sizeof(t), "%s", a.s(0));
    char *eq = strchr(t, '=');
    if (!eq) {
        snprintf(o.msg, o.msglen, "Type knob=value, for example burst_start=33");
        return OPE_ARG;
    }
    *eq = 0;
    char *v = eq + 1;
    while (*v == ' ') {
        v++;
    }
    for (char *e = eq; e > t && e[-1] == ' '; e--) {
        e[-1] = 0;
    }
    int k = dt_av_knob_of(t);
    if (k < 0) {
        snprintf(o.msg, o.msglen, "No knob \"%s\" (the rows of the A/V page, '_' for blanks)", t);
        return OPE_NOT_FOUND;
    }
    const AvKnob &kn = dt_av_knobs[k];
    if (*v != '-' && *v != '+' && (*v < '0' || *v > '9')) {
        OpResult r;
        return settings_set(c, kn.key, v, &r) == OPE_OK ? OPE_OK : (snprintf(o.msg, o.msglen, "%s has no value \"%s\"", kn.key, v), OPE_ARG);
    }
    int n = atoi(v);
    if (n < kn.lo || n > kn.hi) {
        snprintf(o.msg, o.msglen, "%s takes %d to %d", kn.key, (int)kn.lo, (int)kn.hi);
        return OPE_ARG;
    }
    av_put_raw(c, k, n);
    snprintf(o.msg, o.msglen, "%s = %d", kn.key, n);
    return OPE_OK;
}

static void run_path(CoreCtx *c, const char *path)
{
    static PdUi ui;
    static DiagOutcome out;
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
        g_host->notice("Port Diagnostics", out.msg);
    } else {
        snprintf(g_last, sizeof(g_last), "%s: %d pass, %d fail, %d refused, %d error", fn ? fn + 1 : path, out.npass, out.nfail, out.nrefused, out.nerror);
    }
}

static void worker(void *arg)
{
    CoreCtx *c = (CoreCtx *)arg;
    g_ctx = c;
    if (!g_path[0]) {                                  // the task's own picker
        char start[220];
        snprintf(start, sizeof(start), "%s/tests", g_folder);
        if (!g_host->pick(start, g_path, sizeof(g_path))) {
            snprintf(g_last, sizeof(g_last), "no test picked");
            g_busy = false;
            return;
        }
    }
    run_path(c, g_path);
    g_path[0] = 0;
    g_busy = false;
}

static int start(CoreCtx *c, const char *path, OpOut &o)
{
    if (!g_host) {
        snprintf(o.msg, o.msglen, "This build has no dialogs for the runner");
        return OPE_UNSUPPORTED;
    }
    if (g_busy) {
        snprintf(o.msg, o.msglen, "A test is running");
        return OPE_BUSY;
    }
    folder_of(c);
    g_ctx = c;
    snprintf(g_path, sizeof(g_path), "%s", path);
    g_busy = true;
    if (!g_host->spawn(worker, c)) {
        g_busy = false;
        snprintf(o.msg, o.msglen, "The runner's task did not start");
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, "Test started");
    return OPE_OK;
}

static int op_run(CoreCtx *c, const OpArgs &a, OpOut &o) { return start(c, a.s(0), o); }
static int op_pick(CoreCtx *c, const OpArgs &, OpOut &o) { return start(c, "", o); }
static int op_install(CoreCtx *c, const OpArgs &, OpOut &o)
{
    folder_of(c);
    int n = c->install_files ? c->install_files(c) : -1;          // record 8 of diag.cor (instfiles.h)
    if (n < 0) {
        snprintf(o.msg, o.msglen, "Cannot write %s/tests (read only card?)", g_folder);
        return OPE_FAILED;
    }
    snprintf(o.msg, o.msglen, n ? "%d example test(s) written to %s/tests" : "All example tests are there already", n, g_folder);
    return OPE_OK;
}

// ---- the start -------------------------------------------------------------------------------------------------------------------------------
static int pd_init(CoreCtx *c, char *, int)
{
    c->io->wr(PD_CTRL, PD_CTRL_ABORT);                 // every output released
    c->io->wr(PD_CTRL, PD_CTRL_CLEAR);
    folder_of(c);
    g_ctx = c;
    for (int k = 0; k < DT_AV_N; k++) {
        g_av_raw[k] = dt_av_knobs[k].val[av_page_idx(c, k)];       // what the page has put into the registers
    }
    return 0;
}

static int pd_release(CoreCtx *c, char *, int)
{
    c->io->wr(PD_CTRL, PD_CTRL_ABORT);
    return 0;
}

static const OpBind run_bind[] = { B_FILE("path") };
static const OpBind load_bind[] = { B_FILE("path") };
static const OpBind save_bind[] = { B_TEXT("name") };
static const OpBind free_bind[] = { B_TEXT("assign") };
static const FileAction toml_actions[] = {
    { "Run test", "pd.run", run_bind, 1, false, 0, 0, 0, 0, 'R' },
    { "Load A/V preset", "pd.av_load", load_bind, 1, false, 0, 0, 0, 0, 'L' },
};
static const char *const pd_caps[] = { "pad" };
static const FileType filetypes[] = {
    { ".toml", toml_actions, 2, 0, 0, 0 },
};
static const Task tasks[] = {
    { "Pick a test and run it", "pd.pick", 0, 0, false },
    { "Write the example tests", "pd.install", 0, 0, false },
    { "Save A/V preset", "pd.av_save", save_bind, 1, false, "Preset name", 0 },
    { "Load A/V preset by name", "pd.av_preset", save_bind, 1, false, "Preset name (Sony PAL, NTSC-50.rix01, NTSC-60.rix02 or your own)", 0 },
    { "Set an A/V knob (playground)", "pd.av_free", free_bind, 1, false, "knob=value, for example burst_start=33", 0 },
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
    r.add_op("pd.install", 0, 0, op_install);
    static const ArgSpec load_args[] = { ARG_PATH("path") };
    static const ArgSpec save_args[] = { ARG_STRING("name", 41) };
    r.add_op("pd.av_load", load_args, 1, op_av_load);
    r.add_op("pd.av_save", save_args, 1, op_av_save);
    static const ArgSpec free_args[] = { ARG_STRING("assign", 63) };
    r.add_op("pd.av_preset", save_args, 1, op_av_preset);
    r.add_op("pd.av_free", free_args, 1, op_av_free);
    for (int k = 0; k < DT_AV_N; k++) {
        r.add_op(av_op_names[k], g_av_args[k], 1, av_ops[k]);
    }
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
    g_av_settings, DT_AV_N,
    tasks, 5,
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
