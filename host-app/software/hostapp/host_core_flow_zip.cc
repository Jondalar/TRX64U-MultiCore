// Unzip in the File Browser (host_core_flow_zip.h). The order of the pages is this file's; finding, loading and running the application is coreapp/src's
// (appindex.cc, libload.cc), the same code a core runs. The texts of the application's own questions are the application's.
#include "host_core_flow_zip.h"
#include "host_core_flow_util.h"
#include "appindex.h"
#include "ops.h"
#include "ctx.h"
#include "keyboard.h"
#include "mediaui.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

using namespace hcf;

static int n_zip_runs;
static char last_zip_why[160];

int hc_zip_runs(void) { return n_zip_runs; }
const char *hc_zip_why(void) { return last_zip_why; }

namespace {

#define ZIP_APP "unzip"
#define ZIP_SHARED "/SD/cores/apps"            // APP_SHARED_FOLDER on the card: where the package puts the applications

// What stays between two unzips: a library that was loaded stays in the heap (libraries are resident), so the loader must too.
struct ZipRig {
    OpRegistry reg;
    LibLoader ld;
    AppIndex idx;
    CoreCtx ctx;
    ZipRig(LibHost *h, CoreFs *fs) : ld(h, &reg)
    {
        memset(&ctx, 0, sizeof(ctx));
        ctx.fs = fs;
        ld.set_ctx(&ctx);
    }
};

class ZipJob;
ZipJob *g_job;                                 // the job whose application runs: the service table's functions are plain C pointers

class FlowLog : public LibLog {
public:
    explicit FlowLog(const HcFlowEnv &e) : e_(e) { }
    void line(const char *t) override { flog(e_, "[zip] %s", t); }

private:
    const HcFlowEnv &e_;
};

class ZipJob : public HcJob {
public:
    ZipJob(HcZipEnv &z, const char *p) : HcJob(*z.base), zenv(z), k(0), rig_(0), ent(0), handle(-1), app_rc(0), said(0)
    {
        snprintf(path, sizeof(path), "%s", p);
        kind = 7;
        reply[0] = err[0] = 0;
        tab_init();
    }
    ~ZipJob() override { if (g_job == this) g_job = 0; }

    // ---- what the application asks while it runs (called from inside run(), on this task)
    void ask_message(const char *title, const char *text);
    int ask_choose(const char *title, const char *const *items, int n, int sel);
    int ask_buttons(const char *title, const char *text, int buttons);
    void *bar_open(const char *title, int total);
    void bar_set(void *h, int value, const char *text);

protected:
    bool run() override;

private:
    HcZipEnv &zenv;
    char path[200], file[56], title[72], l0[124], l1[124], act[16];
    HcPage pg;
    int k;
    ZipRig *rig_;
    const AppEntry *ent;
    int handle, app_rc, said;
    char folders[2][APP_PATH_MAX];
    int nf;
    char reply[240], err[LIB_ERR_MAX];
    int bar_total;
    char bar_title[64];
    void tab_init();
    int wait_key();                                 // blocking: only for the application's questions (the flow itself waits in steps)
    void refuse(const char *a, const char *b);
    void draw_bar(int value, const char *text, bool force);
};

void ZipJob::refuse(const char *a, const char *b)
{
    state = HCJ_RESULT;
    snprintf(title, sizeof(title), "Cannot unzip %s", file);
    hp_page(&pg, HCT_WARN, title);
    hp_line(&pg, 0, HCT_WARN, a);
    const char *nl = b ? strchr(b, '\n') : 0;
    if (nl) {                                                   // a second sentence on its own line, so that no line is cut to dots
        char first[124];
        snprintf(first, sizeof(first), "%.*s", (int)(nl - b), b);
        hp_line(&pg, 1, HCT_INK, first);
        hp_line(&pg, 2, HCT_INK, nl + 1);
    } else if (b && b[0]) {
        hp_line(&pg, 1, HCT_INK, b);
    }
    hp_line(&pg, 4, HCT_INK, "Nothing was unpacked.");
    snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    snprintf(last_zip_why, sizeof(last_zip_why), "%s", a);
    draw(env, pg);
}

int ZipJob::wait_key()
{
    for (;;) {
        int c = env.getch(env.ctx);
        if (c >= 0) {
            return c;
        }
        idle(env);
        sleep_ms(env, 20);
    }
}

void ZipJob::ask_message(const char *t, const char *text)
{
    hp_page(&pg, HCT_HOST, t && t[0] ? t : "Unzip");
    int n = 0;
    for (const char *p = text ? text : ""; *p && n < 6; n++) {            // the text in lines: at a newline, else 100 characters, cut at a space where there is one
        int len = 0;
        while (p[len] && p[len] != '\n') len++;
        int cut = len > 100 ? 100 : len;
        if (len > 100) {
            for (int i = cut; i > 60; i--) {
                if (p[i] == ' ') {
                    cut = i;
                    break;
                }
            }
        }
        char ln[124];
        snprintf(ln, sizeof(ln), "%.*s", cut, p);
        hp_line(&pg, n, HCT_INK, ln);
        p += cut;
        while (*p == ' ' || *p == '\n') p++;
    }
    snprintf(pg.hint, sizeof(pg.hint), "any key continues");
    draw(env, pg);
    hook(env, -3, 0, 0);                                        // the application's page is up
    wait_key();
    said++;
}

int ZipJob::ask_buttons(const char *t, const char *text, int buttons)
{
    int yes = (buttons & SVC_BUTTON_YES) != 0;
    hp_page(&pg, HCT_WARN, t && t[0] ? t : "Unzip");
    hp_line(&pg, 0, HCT_INK, text ? text : "");
    snprintf(pg.hint, sizeof(pg.hint), yes ? "RETURN yes    any other key no" : "RETURN continues    ESC cancels");
    draw(env, pg);
    int c = wait_key();
    if (c == KEY_RETURN) {
        return yes ? SVC_BUTTON_YES : SVC_BUTTON_OK;
    }
    if (yes && c != KEY_ESCAPE && c != KEY_BREAK && c != KEY_LEFT) {
        return (buttons & SVC_BUTTON_NO) ? SVC_BUTTON_NO : 0;
    }
    return yes && (buttons & SVC_BUTTON_NO) ? SVC_BUTTON_NO : 0;
}

int ZipJob::ask_choose(const char *t, const char *const *items, int n, int sel)
{
    if (n <= 0) {
        return -1;
    }
    if (sel < 0 || sel >= n) {
        sel = 0;
    }
    const int rows = 6;
    int first = 0;
    for (;;) {
        if (sel < first) first = sel;
        if (sel >= first + rows) first = sel - rows + 1;
        hp_page(&pg, HCT_HOST, t && t[0] ? t : "Choose");
        for (int i = 0; i < rows && first + i < n; i++) {
            char ln[124];
            snprintf(ln, sizeof(ln), "%s %s", first + i == sel ? ">" : " ", items[first + i]);
            hp_line(&pg, i, first + i == sel ? HCT_OK : HCT_INK, ln);
        }
        snprintf(pg.pct, sizeof(pg.pct), "%d / %d", sel + 1, n);
        snprintf(pg.hint, sizeof(pg.hint), "UP/DOWN select    RETURN takes it    ESC cancels");
        draw(env, pg);
        hook(env, -4, (uint64_t)sel, (uint64_t)n);               // the list is up
        int c = wait_key();
        if (c == KEY_RETURN) return sel;
        if (c == KEY_ESCAPE || c == KEY_BREAK || c == KEY_LEFT) return -1;
        if (c == KEY_UP && sel > 0) sel--;
        else if (c == KEY_DOWN && sel + 1 < n) sel++;
    }
}

void ZipJob::draw_bar(int value, const char *text, bool force)
{
    hp_page(&pg, HCT_HOST, bar_title);
    hp_line(&pg, 0, HCT_INK, file);
    snprintf(pg.msg, sizeof(pg.msg), "%s", text && text[0] ? text : "");
    pg.mcol = HCT_MUTED;
    pg.bar = bar_total > 0 ? (int)((int64_t)value * 100 / bar_total) : 0;
    if (pg.bar > 100) pg.bar = 100;
    snprintf(pg.hint, sizeof(pg.hint), "unpacking    no key stops it");
    draw(env, pg, force);
}

void *ZipJob::bar_open(const char *t, int total)
{
    snprintf(bar_title, sizeof(bar_title), "%s", t && t[0] ? t : "Unzipping");
    bar_total = total;
    draw_bar(0, "", true);
    return this;
}

void ZipJob::bar_set(void *, int value, const char *text)
{
    draw_bar(value, text, value >= bar_total);
    idle(env);
}

// the service table: plain C functions, the job is found through g_job
void s_message(const char *t, const char *text) { if (g_job) g_job->ask_message(t, text); }
int s_buttons(const char *t, const char *text, int b) { return g_job ? g_job->ask_buttons(t, text, b) : 0; }
bool s_confirm(const char *t, const char *text) { return g_job && g_job->ask_buttons(t, text, SVC_BUTTON_YES | SVC_BUTTON_NO) == SVC_BUTTON_YES; }
int s_choose(const char *t, const char *const *items, int n, int sel) { return g_job ? g_job->ask_choose(t, items, n, sel) : -1; }
int s_choose_keys(const char *t, const char *const *items, int n, const char *) { return s_choose(t, items, n, 0); }
bool s_text(const char *, char *, int) { return false; }                                  // not on the host: the application is handed the file
bool s_pick_file(const char *, const char *, char *, int) { return false; }
bool s_pick_folder(const char *, char *, int) { return false; }
void *s_bar_open(const char *t, int total) { return g_job ? g_job->bar_open(t, total) : 0; }
void s_bar_set(void *h, int v, const char *text) { if (g_job) g_job->bar_set(h, v, text); }
void s_bar_close(void *) { }

SvcDlg g_tab;

void ZipJob::tab_init()
{
    g_tab.size = sizeof(g_tab);
    g_tab.message = s_message;
    g_tab.buttons = s_buttons;
    g_tab.confirm = s_confirm;
    g_tab.choose = s_choose;
    g_tab.choose_keys = s_choose_keys;
    g_tab.text = s_text;
    g_tab.pick_file = s_pick_file;
    g_tab.pick_folder = s_pick_folder;
    g_tab.progress_open = s_bar_open;
    g_tab.progress_set = s_bar_set;
    g_tab.progress_close = s_bar_close;
}

// the first part of a path: "/SD" of "/SD/games/x.zip"
bool medium_of(const char *p, char *out, int n)
{
    if (p[0] != '/') return false;
    const char *e = strchr(p + 1, '/');
    int len = e ? (int)(e - p) : (int)strlen(p);
    if (len < 2 || len >= n) return false;
    memcpy(out, p, len);
    out[len] = 0;
    return true;
}

bool ZipJob::run()
{
    HJ_BEGIN();
    snprintf(file, sizeof(file), "%s", base_of(path));
    {
        char med[24];
        nf = 0;
        if (medium_of(path, med, sizeof(med))) {
            snprintf(folders[nf++], APP_PATH_MAX, "%s/cores/" APP_SHARED_FOLDER, med);
        }
        if (!nf || strcasecmp(folders[0], ZIP_SHARED)) {
            snprintf(folders[nf++], APP_PATH_MAX, "%s", ZIP_SHARED);
        }
    }
    if (!zenv.rig) {
        zenv.rig = new ZipRig(zenv.libhost, zenv.fs);
    }
    rig_ = static_cast<ZipRig *>(zenv.rig);
    {
        FlowLog log(env);
        rig_->idx.scan(zenv.fs, folders, nf, &log);
    }
    ent = rig_->idx.find(ZIP_APP);
    {
        if (!ent) {
            refuse("Unzip is not installed.", "Copy unzip.u2a and miniz.u2a from the release\ninto the folder cores/apps on the SD card.");
            HJ_WAIT_KEY(k);
            return finish(false);
        }
        if (rig_->ld.handle_of(ZIP_APP) < 0 && !rig_->idx.fits(*ent, rig_->ld.room(), &rig_->ld)) {
            refuse("There is not enough memory for Unzip.", "Or a library it needs (miniz.u2a) is missing from the folder.");
            HJ_WAIT_KEY(k);
            return finish(false);
        }
    }
    state = HCJ_ASKING;
    snprintf(title, sizeof(title), "Unzip %s", file);
    hp_page(&pg, HCT_HOST, title);
    hp_line(&pg, 0, HCT_INK, "F   unpack into a folder named after the file");
    hp_line(&pg, 1, HCT_INK, "H   unpack here, next to the .zip");
    hp_line(&pg, 2, HCT_INK, "O   extract one file of the archive");
    snprintf(pg.hint, sizeof(pg.hint), "F, H or O    any other key cancels");
    draw(env, pg);
    for (;;) {
        hook(env, -2, 0, 0);                                    // the question is up
        if ((k = env.getch(env.ctx)) >= 0) {
            break;
        }
        idle(env);
        sleep_ms(env, 20);
        HJ_YIELD();
    }
    if (k == 'f' || k == 'F') {
        snprintf(act, sizeof(act), "folder");
    } else if (k == 'h' || k == 'H') {
        snprintf(act, sizeof(act), "here");
    } else if (k == 'o' || k == 'O') {
        snprintf(act, sizeof(act), "one");
    } else {
        if (env.note) {
            env.note(env.ctx, "Not unpacked. Nothing was changed.", HCT_MUTED);
        }
        return finish(false);
    }
    n_zip_runs++;
    said = 0;
    state = HCJ_RUNNING;
    g_job = this;
    rig_->ld.set_dlg(&g_tab);
    err[0] = reply[0] = 0;
    app_rc = -1;
    {
        const AppEntry *e = ent;
        handle = rig_->ld.handle_of(ZIP_APP);
        if (handle < 0) {
            bool ok = rig_->idx.load_needs(zenv.fs, &rig_->ld, *e, err, sizeof(err));
            CoreFile *f = ok ? zenv.fs->open(e->path, false) : 0;
            if (f) {
                CoreFileSource src(f);
                ok = rig_->ld.load(&src, &handle, err, sizeof(err)) == LIB_OK;
                zenv.fs->close(f);
            } else if (ok) {
                snprintf(err, sizeof(err), "%s: the file cannot be opened.", e->path);
                ok = false;
            }
            if (!ok) {
                handle = -1;
            }
        }
        if (handle >= 0) {
            err[0] = 0;
            int rc = rig_->ld.run(handle, act, path, reply, sizeof(reply), &app_rc, err, sizeof(err));
            if (rc != LIB_OK && !err[0]) {
                snprintf(err, sizeof(err), "Unzip did not run.");
            }
        }
    }
    g_job = 0;
    rig_->ld.set_dlg(0);
    flog(env, "[host] unzip %s (%s): rc %d %s%s", file, act, app_rc, err[0] ? err : "", reply);
    state = HCJ_RESULT;
    if (err[0] || app_rc != 0) {
        snprintf(l0, sizeof(l0), "%s", err[0] ? err : (reply[0] ? reply : "Unzip stopped."));
        hp_page(&pg, HCT_WARN, "Unzip did not finish");
        hp_line(&pg, 0, HCT_WARN, l0);
        hp_line(&pg, 2, HCT_INK, "Files unpacked so far are in the folder. The .zip is untouched.");
        snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
        snprintf(last_zip_why, sizeof(last_zip_why), "%s", l0);
        draw(env, pg);
        HJ_WAIT_KEY(k);
        return finish(false);
    }
    if (!reply[0] && said) {                                    // the application said how it went (its own page, answered): nothing more to show
        return finish(true);
    }
    if (!reply[0]) {                                            // the application ended without a word: the person left the list
        if (env.note) {
            env.note(env.ctx, "Not unpacked. Nothing was changed.", HCT_MUTED);
        }
        return finish(false);
    }
    snprintf(l0, sizeof(l0), "%.120s", reply);
    hp_page(&pg, HCT_OK, "Unzipped");
    hp_line(&pg, 0, HCT_OK, l0);
    snprintf(l1, sizeof(l1), "%s", file);
    hp_line(&pg, 1, HCT_MUTED, l1);
    snprintf(pg.hint, sizeof(pg.hint), "any key returns to the File Browser");
    draw(env, pg);
    HJ_WAIT_KEY(k);
    return finish(true);
    HJ_END();
}

}  // namespace

HcJob *hc_zip_job(HcZipEnv &env, const char *path) { return new ZipJob(env, path); }

void hc_zip_rig_free(HcZipEnv &env)
{
    delete static_cast<ZipRig *>(env.rig);
    env.rig = 0;
}
