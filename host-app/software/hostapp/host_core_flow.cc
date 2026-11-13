// The host application's flows as the core runs them (host_core_flow.h). The text of every page is the old hostui.cc's, word for word (the text is the parity
// contract); what changed is where it is built: here, as data, against an environment that does the drawing and the key reading.
#include "host_core_flow.h"
#include "host_core_flow_util.h"
#include "fwconfirm.h"
#include "mediaui.h"
#include "cor.h"
#include "keyboard.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

// ---- the job base (host_core_job.h) ----------------------------------------------------------------------------------------------------------------
bool hc_is_zip_name(const char *name)
{
    size_t l = name ? strlen(name) : 0;
    return l > 4 && !strcasecmp(name + l - 4, ".zip");
}

namespace {
HcJob *g_cur_job;
}

HcJob *hc_job_current(void) { return g_cur_job; }

void HcJob::note_page(const HcPage &p)
{
    page = p;
    pages++;
}

int HcJob::poll_key()
{
    if (!in_used_ && in_.key >= 0) {
        in_used_ = true;
        return in_.key;
    }
    return env.getch(env.ctx);
}

int HcJob::poll_key_nowait()
{
    if (!in_used_ && in_.key >= 0) {
        in_used_ = true;
        return in_.key;
    }
    return env.getch_nowait(env.ctx);
}

bool HcJob::step(const HcJobIn &in)
{
    if (state == HCJ_DONE) {
        return true;
    }
    in_ = in;
    if (pc == 0) {
        in_.key = -1;                                    // the first step starts the flow: no wait is up yet, a key given with it is not an answer to anything
    }
    in_used_ = false;
    HcJob *prev = g_cur_job;
    g_cur_job = this;
    bool d = run();
    g_cur_job = prev;
    return d;
}

bool hc_job_run(HcJob *j)
{
    HcJobIn in;
    in.key = -1;
    while (!j->step(in)) {
    }
    bool ok = j->ok;
    delete j;
    return ok;
}

#define RESTART_COUNTDOWN_MS 5000
#define RESTART_ALIVE_MS 5000                          // the power cycle was sent and the board still runs this long: it did not work

namespace hcf {

void flog(const HcFlowEnv &e, const char *fmt, ...)
{
    if (!e.log) {
        return;
    }
    char b[240];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof(b), fmt, ap);
    va_end(ap);
    e.log(e.ctx, b);
}

void hook(const HcFlowEnv &e, int phase, uint64_t a, uint64_t b)
{
    if (e.hook) {
        e.hook(e.ctx, phase, a, b);
    }
}

void draw(const HcFlowEnv &e, const HcPage &pg, bool force)
{
    if (HcJob *j = hc_job_current()) {
        j->note_page(pg);
    }
    e.draw(e.ctx, pg, force);
}
void idle(const HcFlowEnv &e) { e.idle(e.ctx); }
void sleep_ms(const HcFlowEnv &e, int ms) { e.sleep_ms(e.ctx, ms); }

const char *base_of(const char *path)
{
    const char *s = strrchr(path, '/');
    return s ? s + 1 : path;
}

// "NN %": the percent sign does not go through the format (the firmware's printf has no %%, it prints both characters)
const char *fw_pct(char *out, int n, unsigned pct)
{
    snprintf(out, n, "%u", pct);
    strncat(out, " %", n - strlen(out) - 1);
    return out;
}

// A reason as a sentence: the checks name what failed in lower case, the pages write sentences.
const char *fw_sentence(char *out, int n, const char *why)
{
    snprintf(out, n, "%s", why && why[0] ? why : "No reason given");
    if (out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A');
    }
    int l = (int)strlen(out);
    if (l > 0 && l + 1 < n && out[l - 1] != '.' && out[l - 1] != '?') {
        out[l] = '.';
        out[l + 1] = 0;
    }
    return out;
}

// The second line of a refusal after the check: a file that is too big for its slot is not damaged, so it does not say so.
const char *fw_why_line(const char *why)
{
    return why && strstr(why, "does not fit") ? "The file is intact, but it is too big for its place in the flash." : "The file is damaged or is not what it says.";
}

FwcKey fw_key_class(int k)
{
    if (k == 'y' || k == 'Y') return FWCK_Y;
    if (k == KEY_RETURN) return FWCK_RETURN;
    if (k == KEY_ESCAPE || k == KEY_BREAK) return FWCK_ESCAPE;
    return FWCK_OTHER;
}

// The keys that end a read or the write of the new file: ESC, RUN/STOP and the left arrow (the host's one "back")
bool abort_key(const HcFlowEnv &e)
{
    int k;
    while ((k = e.getch_nowait(e.ctx)) >= 0) {
        if (k == KEY_BREAK || k == KEY_ESCAPE || k == KEY_LEFT) {
            return true;
        }
    }
    return false;
}

// A page that says no, before anything is written: amber (red is for the pages that destroy), one reason, then what did not happen.
static const char *FW_TAIL_FLASH = "Nothing was written. The flash is as it was.";
void fw_refuse_draw(const HcFlowEnv &e, const char *title, const char *l0_in, const char *l1, const char *l2)
{
    HcPage pg;
    char l0[124];
    fw_sentence(l0, sizeof(l0), l0_in);
    hp_page(&pg, HCT_WARN, title);
    int n = 0;
    hp_line(&pg, n++, HCT_WARN, l0);
    if (l1 && l1[0]) {
        hp_line(&pg, n++, HCT_INK, l1);
    }
    if (l2 && l2[0]) {
        hp_line(&pg, n++, HCT_MUTED, l2);
    }
    hp_line(&pg, n, HCT_INK, FW_TAIL_FLASH);
    snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    if (e.refused) {
        e.refused(e.ctx, l0);
    }
    draw(e, pg);
}

bool abort_cb(void *ctx) { return abort_key(*((ReadUi *)ctx)->env); }       // the flows hand the reading page's context to both

void read_cb(void *ctx, uint32_t done, uint32_t total)
{
    ReadUi *u = (ReadUi *)ctx;
    const HcFlowEnv &e = *u->env;
    HcPage pg;
    hp_page(&pg, HCT_HOST, u->title);
    hp_line(&pg, 0, HCT_INK, u->what);
    char a[64], b[64], t[124];
    ui_size_text(a, sizeof(a), done);
    ui_size_text(b, sizeof(b), total);
    snprintf(t, sizeof(t), "%s of %s", a, b);
    hp_line(&pg, 1, HCT_MUTED, t);
    pg.bar = total ? (int)((uint64_t)done * 100 / total) : 100;
    fw_pct(pg.pct, sizeof(pg.pct), (unsigned)pg.bar);
    snprintf(pg.hint, sizeof(pg.hint), "ESC or LEFT cancels    nothing is written");
    draw(e, pg, done == 0 || done >= total);
    idle(e);
    hook(e, -6, done, total);                                 // the bench takes the picture of the check page
}

// ---- the two shared waits as steps (host_core_flow_util.h) ------------------------------------------------------------------------------------------
// Both questions. The body of the old fw_ask / up_ask loop: each pass reads a key (or the clock), idles, and ends with the answer or waits again.
bool AskSub::step(HcJob &j)
{
    const HcFlowEnv &e = *env;
    switch (pc) {
    case 0:
        fwc_open(&c, e.now(e.ctx));
        page(pg, ask, c, e.now(e.ctx));
        draw(e, pg);
        hook(e, -3, 0, 0);                                        // the first question is up
        hooked = c.state;
        shown_left = 0;
        for (;;) {
            {
                int k = j.poll_key_nowait();
                uint32_t t = e.now(e.ctx);
                FwcEvent ev = FWCE_NOTHING;
                if (k > 0) {
                    ev = fwc_key(&c, fw_key_class(k), t);
                } else {
                    sleep_ms(e, 20);
                    ev = fwc_tick(&c, t);
                    if (c.state == FWC_ASK2) {
                        uint32_t left = (uint32_t)fwc_left(&c, t);
                        if (left != shown_left) {
                            ev = FWCE_REDRAW;
                        }
                        shown_left = left;
                    }
                }
                idle(e);
                if (ev == FWCE_GO) {
                    go = true;
                    pc = -1;
                    return true;
                }
                if (ev == FWCE_ABORT) {
                    go = false;
                    pc = -1;
                    return true;
                }
                if (ev == FWCE_REDRAW) {
                    page(pg, ask, c, t);
                    draw(e, pg);
                    if (c.state != hooked) {
                        hooked = c.state;
                        hook(e, -4, 0, 0);                        // the last question is up
                    }
                }
            }
            pc = 1;
            return false;
    case 1:;
        }
    }
    return true;
}

// T152 D11: a written firmware or update starts with the next power-up. The page counts down and then asks the ESP32 to power-cycle the board; a key does it at once.
bool RestartSub::step(HcJob &j)
{
    const HcFlowEnv &e = *env;
    char a[124];
    switch (pc) {
    case 0: {
        while (e.getch_nowait(e.ctx) >= 0) {
        }                                                  // a key from before the page is not an answer to it
        snprintf(pg.hint, sizeof(pg.hint), "any key restarts now");
        t0 = e.now(e.ctx);
        shown = -1;
    }
        for (;;) {
            {
                uint32_t el = e.now(e.ctx) - t0;
                int left = el >= RESTART_COUNTDOWN_MS ? 0 : (int)((RESTART_COUNTDOWN_MS - el + 999) / 1000);
                if (left == 0) {
                    break;
                }
                if (left != shown) {
                    shown = left;
                    snprintf(a, sizeof(a), "Restarting in %d s to start %s.", left, what);
                    hp_line(&pg, line, HCT_INK, a);
                    draw(e, pg);
                    hook(e, -7, (uint64_t)left, 0);            // the bench looks at the page and may press a key
                }
                if (j.poll_key_nowait() >= 0) {
                    break;
                }
                idle(e);
                sleep_ms(e, 20);
            }
            pc = 1;
            return false;
    case 1:;
        }
        why = NULL;
        hp_line(&pg, line, HCT_INK, "Restarting the board now.");
        pg.hint[0] = 0;
        draw(e, pg);
        if (e.power_cycle(e.ctx, &why)) {
            flog(e, "[host] power cycle sent to the control module");
            t1 = e.now(e.ctx);
            while (e.now(e.ctx) - t1 < RESTART_ALIVE_MS) {
                sleep_ms(e, 20);
                pc = 2;
                return false;
    case 2:;
            }
            why = "the board is still powered after the power cycle was sent";
        }
        flog(e, "[host] power cycle not done: %s", why);
        snprintf(a, sizeof(a), "The restart did not happen: %s.", why);
        hp_line(&pg, line - 1, HCT_WARN, a);
        snprintf(a, sizeof(a), "Switch the power off and on to start %s.", what);
        hp_line(&pg, line, HCT_INK, a);
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
        draw(e, pg);
        pc = 3;
    case 3: {
        hook(e, -1, 0, 0);
        if (j.poll_key() < 0) {
            idle(e);
            sleep_ms(e, 20);
            return false;
        }
    }
        pc = -1;
        return true;
    }
    return true;
}

}  // namespace hcf
using namespace hcf;

namespace {

int g_up_writes;

// ---- the update ----------------------------------------------------------------------------------------------------------------------------------------
TxuInfo up_info;
TxuStaged up_staged;
TxuSurvey up_survey;

// The cores on the media (D1): partial cores are bound to the host key, large cores to nothing of ours. new_key = the key after the update.
void up_survey_cores(const HcFlowEnv &e, uint32_t new_key, TxuSurvey *s)
{
    memset(s, 0, sizeof(*s));
    static HostDirEntry media[8], cores[48];
    int nm = e.list(e.ctx, "/", media, 8, false);
    for (int m = 0; m < nm; m++) {
        if (!media[m].dir || !strcasecmp(media[m].name, "flash")) {
            continue;
        }
        char dirp[80];
        snprintf(dirp, sizeof(dirp), "/%s/cores", media[m].name);
        int n = e.list(e.ctx, dirp, cores, 48, true);
        for (int i = 0; i < n; i++) {
            size_t l = strlen(cores[i].name);
            if (cores[i].dir || l < 4 || strcasecmp(cores[i].name + l - 4, ".cor")) {
                continue;
            }
            char p[160];
            snprintf(p, sizeof(p), "%s/%s", dirp, cores[i].name);
            CorSource *fs = e.open(e.ctx, p);
            static CorInfo ci;
            if (fs) {
                txu_survey_add(s, cor_probe(*fs, ci), ci, cor_host_key, new_key);
                delete fs;
            }
        }
    }
}

struct UpAsk {
    char title[72];
    char version[124];
    char what[124];           // "This replaces the host app in the flash."
    char cores[124];          // the effect on the cores
    uint8_t cores_col;
    char cores2[124];         // a second line about the cores, or ""
    char skip[124];           // what is not written again
    char chip[32];
    char last[124];           // question 2: "The host app in the flash is erased and written."
};

void up_ask_page(HcPage &pg, const UpAsk &a, const FwConfirm &c, uint32_t now)
{
    hp_page(&pg, HCT_BAD, a.title);
    int n = 0;
    if (c.state == FWC_ASK2) {
        hp_line(&pg, n++, HCT_BAD, "Last question.");
        hp_line(&pg, n++, HCT_INK, a.last);
        hp_line(&pg, n++, HCT_WARN, "From the first erase on, no key stops the write.");
        hp_line(&pg, n++, HCT_MUTED, "The power must stay on until the result.");
        snprintf(pg.hint, sizeof(pg.hint), "RETURN starts the write (%d s)    any other key cancels", fwc_left(&c, now));
        return;
    }
    hp_line(&pg, n++, HCT_BAD, a.what);
    hp_line(&pg, n++, HCT_INK, a.version);
    hp_line(&pg, n++, HCT_OK, "The package is complete and intact.");
    hp_line(&pg, n++, a.cores_col, a.cores);
    if (a.cores2[0]) {
        hp_line(&pg, n++, HCT_WARN, a.cores2);
    }
    if (a.skip[0]) {
        hp_line(&pg, n++, HCT_MUTED, a.skip);
    }
    char t[124];
    snprintf(t, sizeof(t), "Flash chip: %s. Nothing has been written yet.", a.chip);
    hp_line(&pg, n++, HCT_MUTED, t);
    snprintf(pg.hint, sizeof(pg.hint), "Y continues    any other key cancels");
}

struct UpRunUi {
    const HcFlowEnv *env;
    char title[72];
    uint32_t parts;                        // FW_PART_* being written
    uint32_t a_sec, b_sec, rb_pages;
    int phase;
    char detail[48];
    uint32_t done;
    uint32_t msg_until;
    bool msg_on;
};

void up_ask_page_cb(HcPage &pg, const void *ask, const FwConfirm &c, uint32_t now) { up_ask_page(pg, *(const UpAsk *)ask, c, now); }

const char *up_phase_name[FWP_PHASES] = { "Write the host app", "Write the host", "Read back and compare" };

void up_run_page(const UpRunUi &u, bool force)
{
    const HcFlowEnv &e = *u.env;
    HcPage pg;
    hp_page(&pg, HCT_HOST, u.title);
    uint32_t t = e.now(e.ctx);
    hp_line(&pg, 0, HCT_OK, "Check the file   done");
    for (int i = 0; i < FWP_PHASES; i++) {
        char l[124];
        int col = HCT_MUTED;
        bool wanted = i == FWP_READBACK || (i == FWP_APP ? (u.parts & FW_PART_APP) : (u.parts & FW_PART_BIT));
        if (!wanted) {
            snprintf(l, sizeof(l), "%s   not needed, unchanged", up_phase_name[i]);
        } else if (i < u.phase) {
            snprintf(l, sizeof(l), "%s   done", up_phase_name[i]);
            col = HCT_OK;
        } else if (i == u.phase) {
            snprintf(l, sizeof(l), "%s   ...", up_phase_name[i]);
            col = HCT_INK;
        } else {
            snprintf(l, sizeof(l), "%s", up_phase_name[i]);
        }
        hp_line(&pg, 1 + i, col, l);
    }
    hp_line(&pg, 5, HCT_MUTED, u.detail);                      // row 4 stays empty: what is being done does not read as a fifth step
    uint32_t units = u.a_sec + u.b_sec + u.rb_pages / 16 + 1, base = u.phase == FWP_APP ? 0 : (u.phase == FWP_BIT ? u.a_sec : u.a_sec + u.b_sec);
    uint32_t d = u.phase == FWP_READBACK ? u.done / 16 : u.done;
    pg.bar = (int)((uint64_t)(base + d) * 100 / units);
    if (pg.bar > 100) {
        pg.bar = 100;
    }
    fw_pct(pg.pct, sizeof(pg.pct), (unsigned)pg.bar);
    pg.mcol = HCT_WARN;
    if (t < u.msg_until) {
        snprintf(pg.msg, sizeof(pg.msg), "Cannot stop now. Wait for the result.");
    }
    snprintf(pg.hint, sizeof(pg.hint), "No key stops this    do not switch the power off");
    draw(e, pg, force);
}

void up_write_cb(void *ctx, int phase, uint32_t done, uint32_t total, const char *detail)
{
    UpRunUi *u = (UpRunUi *)ctx;
    const HcFlowEnv &e = *u->env;
    bool newphase = phase != u->phase;
    u->phase = phase;
    u->done = done;
    snprintf(u->detail, sizeof(u->detail), "%s", detail);
    if (total && phase != FWP_READBACK && (!strcmp(detail, "erasing") || !strcmp(detail, "programming"))) {
        snprintf(u->detail, sizeof(u->detail), "%s sector %u of %u", detail, (unsigned)(done + (done < total ? 1 : 0)), (unsigned)total);
    }
    if (u->detail[0] >= 'a' && u->detail[0] <= 'z') {
        u->detail[0] = (char)(u->detail[0] - 'a' + 'A');
    }
    int k;
    uint32_t now = e.now(e.ctx);
    while ((k = e.getch_nowait(e.ctx)) >= 0) {
        if (k == KEY_ESCAPE || k == KEY_BREAK || k == KEY_LEFT) {
            u->msg_until = now + 3000;
        }
    }
    idle(e);
    bool msg_now = e.now(e.ctx) < u->msg_until;
    bool msg_changed = msg_now != u->msg_on;
    u->msg_on = msg_now;
    up_run_page(*u, newphase || done == 0 || done >= total || msg_changed);
    hook(e, -5, phase, total ? done * 100 / total : 100);
}

struct UpDone {
    const char *written;      // "the host app", "the host and the host app"
    const char *skipped;      // "" or "the host"
    uint32_t bit_len, app_len;
    bool host_key_changes;
    int broken;
};

// What the write did, as a page (built and drawn; the waiting is the job's). The ok page ends in the restart page's steps.
void up_result_page_ok(HcPage &pg, int &rl, const UpDone &d, const FwWriteResult &r, uint32_t ms)
{
    char a[124], b[64], c[64];
    hp_page(&pg, HCT_OK, "Update written");
    snprintf(a, sizeof(a), "Written and verified: %s.", d.written);
    hp_line(&pg, 0, HCT_OK, a);
    ui_size_text(b, sizeof(b), d.bit_len);
    ui_size_text(c, sizeof(c), d.app_len);
    int n = 1;
    if (d.skipped[0]) {
        snprintf(a, sizeof(a), "Not written again, the flash held it already: %s.", d.skipped);
        hp_line(&pg, n++, HCT_MUTED, a);
    }
    if (d.host_key_changes) {
        if (d.broken) {
            snprintf(a, sizeof(a), "%d partial core%s need%s a rebuild for the new host. Large cores are not affected.", d.broken, d.broken == 1 ? "" : "s",
                     d.broken == 1 ? "s" : "");
        } else {
            snprintf(a, sizeof(a), "No core on the media needs a rebuild.");
        }
        hp_line(&pg, n++, d.broken ? HCT_WARN : HCT_MUTED, a);
    }
    n++;                                                    // a blank row: the one thing to do stands apart
    rl = n++;
    snprintf(a, sizeof(a), "Took %u s. Boot menu, cores and settings were not touched.", (unsigned)(ms / 1000));
    hp_line(&pg, n, HCT_MUTED, a);
    (void)r;
}

void up_result_page_fail(const HcFlowEnv &e, HcPage &pg, const FwWriteResult &r)
{
    char a[124];
    hp_page(&pg, HCT_BAD, r.started ? "The flash may not boot" : "Nothing was written");
    fw_sentence(a, sizeof(a), r.why);
    hp_line(&pg, 0, HCT_WARN, a);
    snprintf(a, sizeof(a), "Stopped %s %s (address 0x%06X).", r.phase == FWP_READBACK ? "during" : "while writing",
             r.phase == FWP_APP ? "the host app" : (r.phase == FWP_BIT ? "the host" : "the read-back check"), (unsigned)r.sector);
    hp_line(&pg, 1, HCT_INK, a);
    if (r.started) {
        hp_line(&pg, 2, HCT_BAD, "Do not switch the power off.");
        hp_line(&pg, 3, HCT_INK, "RETURN writes it again from the copy in memory.");
        hp_line(&pg, 4, HCT_MUTED, "If you leave, the board needs to be repaired with a JTAG cable.");
        snprintf(pg.hint, sizeof(pg.hint), "RETURN writes again    ESC or LEFT leaves");
    } else {
        hp_line(&pg, 2, HCT_INK, "The flash is as it was.");
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    }
    if (e.write_failed) {
        e.write_failed(e.ctx, r.why ? r.why : "");
    }
}

const char *up_what(bool host, bool app)
{
    return host && app ? "the host and the host app" : (host ? "the host" : "the host app");
}

}  // namespace

void hp_page(HcPage *pg, int border, const char *title)
{
    memset(pg, 0, sizeof(*pg));
    pg->border = (uint8_t)border;
    pg->bar = -1;
    snprintf(pg->title, sizeof(pg->title), "%s", title);
}

void hp_line(HcPage *pg, int i, int tone, const char *text)
{
    snprintf(pg->line[i], sizeof(pg->line[i]), "%s", text);
    pg->lcol[i] = (uint8_t)tone;
}

int hc_update_writes(void) { return g_up_writes; }

// ---- the update as a job -----------------------------------------------------------------------------------------------------------------------------
// Returns true when something was written and verified. Every refusal is a page of its own and ends the job not ok. The straight-line code of the old hc_update_run:
// probing (the file, the checks, the staging) -> asking (the two questions) -> running (the write, no key stops it) -> result (the page that says how it ended).
namespace {

class UpdateJob : public HcJob {
public:
    UpdateJob(const HcFlowEnv &e, const char *p) : HcJob(e), srcp(0)
    {
        snprintf(path, sizeof(path), "%s", p);
        kind = 1;
    }
    ~UpdateJob() { delete srcp; }

protected:
    bool run() override;

private:
    char path[200], file[56], title[72], l0[124];
    CorSource *srcp;
    txu_result_t pr;
    TxuRunning running;
    TxuCheck chk;
    Flash *fl;
    const char *chip, *cwhy;
    ReadUi ru;
    FwImage img;
    uint32_t parts, new_key, t0, ms;
    bool w_host, w_app;
    UpAsk ask;
    AskSub asub;
    UpDone done;
    UpRunUi u;
    FwWriteResult wr;
    HcPage pg;
    int rl, k;
    RestartSub rsub;
};

// a page that says no before anything is written: amber, one reason; it waits for a key and the job ends not ok
#define UP_REFUSE(t, a, b, c) do { state = HCJ_RESULT; fw_refuse_draw(env, t, a, b, c); HJ_WAIT_KEY(k); return finish(false); } while (0)

bool UpdateJob::run()
{
    HJ_BEGIN();
    snprintf(file, sizeof(file), "%s", base_of(path));
    snprintf(title, sizeof(title), "Cannot update from %s", file);
    srcp = env.open(env.ctx, path);
    if (!srcp) {
        UP_REFUSE(title, "The file cannot be read.", "", "");
    }
    pr = txu_probe(*srcp, &up_info);
    if (pr != TXU_OK) {
        UP_REFUSE(title, txu_result_str(pr), pr == TXU_ERR_NOT_TXU ? "Only a .txu from the MultiCore update is installed here." : "", "");
    }
    running.host_key = cor_host_key;
    running.iface_provided = TXU_IFACE_PROVIDED;
    running.iface_needed = TXU_IFACE_NEEDED;
    txu_check(up_info, running, &chk);
    if (!chk.ok) {
        UP_REFUSE(title, chk.why, "This package does not fit the host it would run on.", "Use the update that matches this system.");
    }
    fl = env.flash(env.ctx);
    chip = "none";
    cwhy = "";
    if (!fw_flash_check(fl, &chip, &cwhy)) {
        if (!fw_chip_ok(chip)) {
            snprintf(l0, sizeof(l0), "The update is written for a Winbond W25Q128 flash chip. This board has %s.", strcmp(chip, "none") ? chip : "none that is known");
        } else {
            snprintf(l0, sizeof(l0), "%s", cwhy);
        }
        UP_REFUSE(title, l0, "", "");
    }
    // the check: read into DDR2 and compare every part, nothing is written
    memset(&ru, 0, sizeof(ru));
    ru.env = &env;
    snprintf(ru.title, sizeof(ru.title), "Checking %s", file);
    snprintf(ru.what, sizeof(ru.what), "Reading the file and checking every byte of it");
    read_cb(&ru, 0, 1);
    if (!txu_stage(*srcp, up_info, env.bit_buf(env.ctx), env.app_buf(env.ctx), read_cb, &ru, abort_cb, &up_staged, &img)) {
        if (!strcmp(up_staged.why, "cancelled")) {
            UP_REFUSE(title, "Cancelled while reading the file.", "", "");
        } else {
            UP_REFUSE("Update check failed", up_staged.why, fw_why_line(up_staged.why), "");
        }
    }
    // what the flash holds already is not written again
    parts = 0;
    if (img.bit && !fw_flash_equals_bit(fl, img)) {
        parts |= FW_PART_BIT;
    }
    if (img.app && !fw_flash_equals_app(fl, img)) {
        parts |= FW_PART_APP;
    }
    if (!parts) {
        state = HCJ_RESULT;
        hp_page(&pg, HCT_OK, "Nothing to write");
        hp_line(&pg, 0, HCT_OK, "The flash already holds exactly this update.");
        hp_line(&pg, 1, HCT_MUTED, "Nothing was written.");
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
        draw(env, pg);
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    w_host = (parts & FW_PART_BIT) != 0;
    w_app = (parts & FW_PART_APP) != 0;
    // the effect on the cores (D1): only when the host is written and its key changes; the survey reads the cores' headers only
    new_key = chk.has_host ? up_info.host_key : cor_host_key;
    up_survey_cores(env, new_key, &up_survey);
    memset(&ask, 0, sizeof(ask));
    snprintf(ask.title, sizeof(ask.title), "Update %s?", up_what(w_host, w_app));
    snprintf(ask.what, sizeof(ask.what), "This replaces %s in the flash.", up_what(w_host, w_app));
    {
        char sa[32], sb[32], sz[72];                            // the version and what is written, never a commit id
        ui_size_text(sa, sizeof(sa), img.bit_len);
        ui_size_text(sb, sizeof(sb), img.app_len);
        snprintf(sz, sizeof(sz), "%s%s%s", w_host ? sa : "", w_host && w_app ? " + " : "", w_app ? sb : "");
        snprintf(ask.version, sizeof(ask.version), "Version %s   %s", up_info.version, sz);
    }
    if (w_host && chk.host_key_changes) {
        if (up_survey.broken) {
            snprintf(ask.cores, sizeof(ask.cores), "The host key changes: %d partial core%s need%s a rebuild for the new host.", up_survey.broken,
                     up_survey.broken == 1 ? "" : "s", up_survey.broken == 1 ? "s" : "");
        } else {
            snprintf(ask.cores, sizeof(ask.cores), "The host key changes; no partial core on the media is affected.");
        }
        ask.cores_col = up_survey.broken ? HCT_WARN : HCT_OK;
        snprintf(ask.cores2, sizeof(ask.cores2), "%d large core%s %s not affected.%s", up_survey.large, up_survey.large == 1 ? "" : "s",
                 up_survey.large == 1 ? "is" : "are", up_survey.revived ? " Some cores that are off now start again." : "");
        if (!up_survey.broken && !up_survey.large) {
            ask.cores2[0] = 0;
        }
    } else if (w_host) {
        snprintf(ask.cores, sizeof(ask.cores), "The host key stays the same: no core is affected.");
        ask.cores_col = HCT_OK;
    } else {
        snprintf(ask.cores, sizeof(ask.cores), "Only the app changes: no core is affected.");
        ask.cores_col = HCT_OK;
    }
    if (img.bit && img.app && !(w_host && w_app)) {
        snprintf(ask.skip, sizeof(ask.skip), "Not written again, the flash holds it already: %s.", w_host ? "the host app" : "the host");
    }
    snprintf(ask.chip, sizeof(ask.chip), "%s", chip);
    snprintf(ask.last, sizeof(ask.last), "%s in the flash %s erased and written again.", up_what(w_host, w_app), w_host && w_app ? "are" : "is");
    if (ask.last[0] >= 'a' && ask.last[0] <= 'z') {
        ask.last[0] = (char)(ask.last[0] - 'a' + 'A');
    }
    flog(env, "[host] update from %s asked for: %s, %u + %u bytes staged", file, up_what(w_host, w_app), (unsigned)img.bit_len, (unsigned)img.app_len);
    state = HCJ_ASKING;
    asub.begin(env, &ask, up_ask_page_cb);
    HJ_SUB(asub);
    if (!asub.go) {
        flog(env, "[host] update from %s: not confirmed, nothing written", file);
        env.note(env.ctx, "update cancelled, nothing was written", HCT_MUTED);
        return finish(false);
    }
    memset(&done, 0, sizeof(done));
    done.written = up_what(w_host, w_app);
    done.skipped = (img.bit && img.app && !(w_host && w_app)) ? (w_host ? "the host app" : "the host") : "";
    done.bit_len = w_host ? img.bit_len : 0;
    done.app_len = w_app ? img.app_len : 0;
    done.host_key_changes = w_host && chk.host_key_changes;
    done.broken = up_survey.broken;
    for (;;) {
        state = HCJ_RUNNING;
        can_cancel = false;
        memset(&u, 0, sizeof(u));
        u.env = &env;
        snprintf(u.title, sizeof(u.title), "Updating %s", up_what(w_host, w_app));
        u.parts = parts;
        u.a_sec = w_app ? (img.app_len + FW_SECTOR - 1) / FW_SECTOR : 0;
        u.b_sec = w_host ? (img.bit_len + FW_SECTOR - 1) / FW_SECTOR : 0;
        u.rb_pages = ((w_host ? img.bit_len : 0) + (w_app ? img.app_len : 0) + FW_PAGE - 1) / FW_PAGE;
        u.phase = w_app ? FWP_APP : FWP_BIT;
        snprintf(u.detail, sizeof(u.detail), "Starting");
        up_run_page(u, true);
        g_up_writes++;
        flog(env, "[host] writing the update: host %u bytes at 0x%06X, app %u bytes at 0x%06X", (unsigned)(w_host ? img.bit_len : 0), (unsigned)FW_BIT_ADDR,
             (unsigned)(w_app ? img.app_len : 0), (unsigned)FW_APP_ADDR);
        t0 = env.now(env.ctx);
        fw_write_parts(fl, img, parts, up_write_cb, &u, &wr);
        ms = env.now(env.ctx) - t0;
        flog(env, "[host] update write: %s after %u ms (%u erases, %u programs): %s", wr.ok ? "ok" : "FAILED", (unsigned)ms, (unsigned)wr.erases,
             (unsigned)wr.programs, wr.why ? wr.why : "");
        state = HCJ_RESULT;
        can_cancel = false;
        if (wr.ok) {
            up_result_page_ok(pg, rl, done, wr, ms);
            rsub.begin(env, pg, rl, "the new version");
            HJ_SUB(rsub);
            return finish(true);
        }
        up_result_page_fail(env, pg, wr);
        draw(env, pg);
        hook(env, -8, 0, 0);                                      // the result page of a write that failed is up
        if (!wr.started) {
            HJ_WAIT_KEY(k);
            return finish(false);
        }
        for (;;) {
            k = poll_key();
            if (k == KEY_RETURN) {
                break;                                           // write again from the copy in memory
            }
            if (k == KEY_ESCAPE || k == KEY_BREAK || k == KEY_LEFT) {
                return finish(false);
            }
            if (k < 0) {
                idle(env);
                sleep_ms(env, 20);
            }
            HJ_YIELD();
        }
    }
    HJ_END();
}

}  // namespace

HcJob *hc_update_job(const HcFlowEnv &e, const char *path) { return new UpdateJob(e, path); }

bool hc_update_run(const HcFlowEnv &e, const char *path) { return hc_job_run(new UpdateJob(e, path)); }
