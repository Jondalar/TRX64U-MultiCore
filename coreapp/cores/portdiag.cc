// The "Port Diagnostics" descriptor (T175): a core without a machine - the probe engine, its registers and this application. The idle page is the live
// state of the ten joystick-port pins and the four comparator bits (the home page's media rows, read from the register at every redraw); a test is a .toml
// of /SD/cores/diag/tests, picked in the browser or from the task, run on a task of its own, and leaves <test>_<date>.toml and .txt in results/ (diagtest.h).
#include "ctx.h"
#include "memops.h"
#include "boot.h"
#include "portdiag_regs.h"
#include "portdiag.h"
#include "diagtest.h"
#include "corefolder.h"
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

// ---- the run ---------------------------------------------------------------------------------------------------------------------------------
static CoreCtx *g_ctx;
static uint32_t env_now(void) { return g_ctx->io->now_ms(); }
static void env_delay(uint32_t ms) { g_ctx->io->delay_ms(ms); }
static bool env_stamp(char out[16]) { return g_host && g_host->stamp(out); }

void portdiag_prompt_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn)
{
    snprintf(t, tn, "%s  (%d of %d)", test, idx, total);
    snprintf(x, xn, "%s\n\nYES: done, measure.   NO: skip this step.   ESC: stop the test.", text);
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
    int n = diag_install_examples(c->fs, g_folder);
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
    int n = diag_install_examples(c->fs, g_folder);    // the examples the package cannot carry (T148 D8): written when missing; over a file only when it is an unedited text an earlier version shipped
    printf("[core] diag: %d example test(s) written to %s/tests\n", n, g_folder);
    return 0;
}

static int pd_release(CoreCtx *c, char *, int)
{
    c->io->wr(PD_CTRL, PD_CTRL_ABORT);
    return 0;
}

static const OpBind run_bind[] = { B_FILE("path") };
static const FileAction toml_actions[] = {
    { "Run test", "pd.run", run_bind, 1, false, 0, 0, 0, 0, 'R' },
};
static const FileType filetypes[] = {
    { ".toml", toml_actions, 1, 0, 0, 0 },
};
static const Task tasks[] = {
    { "Pick a test and run it", "pd.pick", 0, 0, false },
    { "Write the example tests", "pd.install", 0, 0, false },
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
    0, 0,
    tasks, 2,
    filetypes, 1,
    media, 4,
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },              // no picture of its own: the application's screens are all there is
    { 0, 0, 200 },                                         // nothing to stop: Freeze does not wait
    pd_init, pd_release, 0, 20,
    0, 0, false,
    0, 0,
    pd_register,
    0, 0,                                                  // caps
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
