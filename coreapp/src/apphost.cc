#include "apphost.h"
#include "ops_glue.h"
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

static AppHost *g_host;

// What the screens of one open application are made of: the context the menu reads (a copy of the core's, with a descriptor that lists nothing of the
// core's own), what contrib_merge made of the application's nodes, and the menu on top of both.
struct AppHost::Session {
    CoreCtx vctx;
    CoreDesc vdesc;
    CoreDesc merged;
    MenuMerge mm;
    MenuCtl *menu;
};

AppHost::AppHost(CoreCtx *c, LibLoader *ld, AppIndex *idx, OsPort *os, CoreLock *lock, MenuCtl *core_menu, MenuHost *mh)
    : c_(c), ld_(ld), idx_(idx), os_(os), lock_(lock), core_menu_(core_menu), mh_(mh), dlg_(os, c->fs), mu_(os->mutex_new()), req_pending_(false), handle_(-1),
      reply_bad_(false), reply_ready_(false), task_alive_(false), task_done_(false), stop_(false), resident_(false), resident_task_started_(false),
      last_draw_(0), s_(0), sess_(0), sess_started_(0), end_hook_(0)
{
    req_app_[0] = req_action_[0] = req_file_[0] = 0;
    name_[0] = action_[0] = file_[0] = reply_[0] = 0;
    memset(&dlg_tab_, 0, sizeof(dlg_tab_));
    memset(&job_tab_, 0, sizeof(job_tab_));
    memset(jobs_, 0, sizeof(jobs_));
    for (int i = 0; i < APPJOB_MAX; i++) {
        jobs_[i].finished = os->sem_new();
    }
    dlg_tab_.size = sizeof(dlg_tab_);
    dlg_tab_.message = dlg_message;
    dlg_tab_.buttons = dlg_buttons;
    dlg_tab_.confirm = dlg_confirm;
    dlg_tab_.choose = dlg_choose;
    dlg_tab_.choose_keys = dlg_choose_keys;
    dlg_tab_.text = dlg_text;
    dlg_tab_.pick_file = dlg_pick_file;
    dlg_tab_.pick_folder = dlg_pick_folder;
    dlg_tab_.progress_open = dlg_progress_open;
    dlg_tab_.progress_set = dlg_progress_set;
    dlg_tab_.progress_close = dlg_progress_close;
    job_tab_.size = sizeof(job_tab_);
    job_tab_.start = job_start;
    job_tab_.cancelled = job_cancelled;
    job_tab_.join = job_join;
    job_tab_.running = job_running;
    job_tab_.stop_requested = job_stop_requested;
}

AppHost::~AppHost()
{
    if (g_host == this) {
        g_host = 0;
    }
}

void AppHost::install()
{
    g_host = this;
    c_->app_open = open_op;
    ld_->set_ctx(c_);
    ld_->set_dlg(ui_available() ? &dlg_tab_ : 0);
    dlg_.set_headless(!ui_available());
    ld_->set_job(&job_tab_);
}

void AppHost::set_machine_groups(const SvcTrx *trx, const SvcNet *net, const SvcC64 *c64, int (*flash_read)(uint32_t, void *, int), void (*handover)(void))
{
    ld_->set_c64(c64);
    ld_->set_trx(trx);
    ld_->set_net(net);
    ld_->set_sys_extra(flash_read, handover);
}

// ---- the request ---------------------------------------------------------------------------------------------------------------------------

int AppHost::open_op(CoreCtx *, const char *app, const char *action, const char *file, char *msg, int msglen)
{
    return g_host ? g_host->request(app, action, file, msg, msglen) : OPE_UNSUPPORTED;
}

namespace {
struct MsgLog : LibLog {
    MsgLog(char *m, int n) : m_(m), n_(n) { }
    void line(const char *t) { snprintf(m_, n_, "%s", strncmp(t, "[app] ", 6) ? t : t + 6); }
    char *m_;
    int n_;
};
}

int AppHost::request(const char *app, const char *action, const char *file, char *msg, int msglen)
{
    const AppEntry *e = 0;
    size_t fl = file ? strlen(file) : 0;
    if ((!app || !app[0]) && fl > 4 && !strcasecmp(file + fl - 4, ".u2a")) {      // a .u2a opened in the Library: the file is the application
        MsgLog ml(msg, msglen);
        msg[0] = 0;
        e = idx_->adhoc(c_->fs, file, &ml);
        if (!e) {
            if (!msg[0]) {
                snprintf(msg, msglen, "This is not an application file.");
            }
            return OPE_FORMAT;
        }
        file = "";
        action = "";
    } else {
        e = idx_->find(app);
    }
    if (!e) {
        snprintf(msg, msglen, "There is no application \"%s\" in the apps folder of this core.", app);
        return OPE_NOT_FOUND;
    }
    os_->lock(mu_);
    int rc = OPE_OK;
    if (req_pending_) {
        snprintf(msg, msglen, "Another application is being opened.");
        rc = OPE_BUSY;
    } else if (handle_ >= 0 && strcasecmp(name_, e->name) && (sess_ || (task_alive_ && !resident_))) {
        snprintf(msg, msglen, "Close %s first.", name_);
        rc = OPE_BUSY;
    } else {
        snprintf(req_app_, sizeof(req_app_), "%s", e->name);
        snprintf(req_action_, sizeof(req_action_), "%s", action ? action : "");
        snprintf(req_file_, sizeof(req_file_), "%s", file ? file : "");
        req_pending_ = true;
        snprintf(msg, msglen, "Opening %s ...", e->name);
    }
    os_->unlock(mu_);
    return rc;
}

bool AppHost::take_reply(char *out, int n, bool *bad)
{
    if (!reply_ready_) {
        return false;
    }
    reply_ready_ = false;
    snprintf(out, n, "%s", reply_);
    *bad = reply_bad_;
    return true;
}

void AppHost::post_status(const char *s, bool bad)
{
    printf("[app] %s\n", s);
    if (core_menu_) {
        core_menu_->say(s, bad);
    }
}

// ---- opening --------------------------------------------------------------------------------------------------------------------------------

void AppHost::open_now()
{
    char app[U2A3_NAME_MAX], action[32], file[FS_PATH_MAX];
    os_->lock(mu_);
    snprintf(app, sizeof(app), "%s", req_app_);
    snprintf(action, sizeof(action), "%s", req_action_);
    snprintf(file, sizeof(file), "%s", req_file_);
    req_pending_ = false;
    os_->unlock(mu_);
    const AppEntry *e = idx_->find(app);
    if (!e) {
        post_status("The application is not there any more.", true);
        return;
    }
    if (handle_ >= 0 && strcasecmp(name_, e->name)) {
        // another application is still up: only a resident one can be (a transient one's screens were closed, request() refused otherwise). It is asked to end,
        // as the firmware did when a person started another app from the menu.
        printf("[app] %s is asked to end for %s\n", name_, e->name);
        stop_ = true;
        uint32_t t0 = c_->io->now_ms();
        while (task_alive_ && !task_done_ && c_->io->now_ms() - t0 < 4000) {
            os_->sleep_ms(10);
        }
        task_alive_ = false;
        task_done_ = false;
        end_application();
        stop_ = false;
    }
    int h = ld_->handle_of(e->name);
    if (h < 0) {
        char derr[LIB_ERR_MAX] = "";
        lock_->lock();                                  // the libraries it needs (T197: miniz), from the same folders; they stay loaded
        bool deps = idx_->load_needs(c_->fs, ld_, *e, derr, sizeof(derr));
        lock_->unlock();
        if (!deps) {
            post_status(derr[0] ? derr : "A library the application needs did not load.", true);
            return;
        }
        CoreFile *f = c_->fs->open(e->path, false);
        if (!f) {
            char m[240];
            snprintf(m, sizeof(m), "%s: the file cannot be opened.", e->path);
            post_status(m, true);
            return;
        }
        CoreFileSource src(f);
        char err[LIB_ERR_MAX] = "";
        lock_->lock();                                  // what it registers must not meet an operation that reads the registry
        int rc = ld_->load(&src, &h, err, sizeof(err));
        lock_->unlock();
        c_->fs->close(f);
        if (rc != LIB_OK) {
            post_status(err[0] ? err : "The application did not load.", true);
            return;
        }
        printf("[app] %s v%u.%u loaded at 0x%08x (%u bytes)\n", e->name, (unsigned)(ld_->info(h)->version >> 16), (unsigned)(ld_->info(h)->version & 0xFFFF),
               (unsigned)ld_->info(h)->image_addr, (unsigned)ld_->info(h)->image_size);
    }
    handle_ = h;
    snprintf(name_, sizeof(name_), "%s", e->name);
    snprintf(action_, sizeof(action_), "%s", action);
    snprintf(file_, sizeof(file_), "%s", file);
    resident_ = ld_->info(h)->residence == U2A3_RES_RESIDENT;
    stop_ = false;
    ld_->set_current(h);
    if (e->has_screens && !sess_) {
        if (!build_session(e)) {
            lock_->lock();
            if (!resident_) {
                ld_->release(h, 0, 0);
            }
            lock_->unlock();
            ld_->set_current(-1);
            handle_ = -1;
            return;
        }
    }
    if (!task_alive_) {
        task_done_ = false;
        reply_[0] = 0;
        task_alive_ = true;
        if (!os_->task_new("App", task_entry, this, 0, 8192)) {
            task_alive_ = false;
            post_status("No task for the application.", true);
            if (sess_) {
                drop_session();
            }
            lock_->lock();
            if (!resident_) {
                ld_->release(h, 0, 0);
            }
            lock_->unlock();
            ld_->set_current(-1);
            handle_ = -1;
        }
    }
}

void AppHost::task_entry(void *self)
{
    AppHost *H = static_cast<AppHost *>(self);
    int rc = 0;
    char err[LIB_ERR_MAX] = "";
    int lrc = H->ld_->run(H->handle_, H->action_, H->file_, H->reply_, sizeof(H->reply_), &rc, err, sizeof(err), true);
    if (lrc != LIB_OK) {
        snprintf(H->reply_, sizeof(H->reply_), "%s", err);
        H->reply_bad_ = true;
    } else {
        H->reply_bad_ = rc != 0;
    }
    H->task_done_ = true;
}

// The screens: the application's own route and nodes, merged into a menu that the person moves in like the core's.
bool AppHost::build_session(const AppEntry *e)
{
    Session *s = new Session;
    memset(&s->mm, 0, sizeof(s->mm));
    s->vctx = *c_;
    s->vdesc = *c_->desc;
    s->vdesc.title = e->name;
    s->vdesc.short_title = e->name;
    s->vdesc.cor_name = e->name;
    s->vdesc.ntasks = s->vdesc.nsettings = s->vdesc.nmedia = s->vdesc.nfiletypes = s->vdesc.nroms = 0;
    s->vdesc.roms = 0;
    s->vdesc.settings = 0;
    s->vdesc.tasks = 0;
    s->vdesc.media = 0;
    s->vdesc.filetypes = 0;
    s->vdesc.romset = 0;
    s->vdesc.menu_rec = 0;
    s->vdesc.menu_rec_n = 0;
    s->vdesc.needs_program = 0;
    s->vdesc.register_ops = 0;
    Contributor ct;
    ct.name = e->name;
    ct.rank = CR_DESC;
    ct.rec = &e->screens;
    int refused = contrib_merge(&s->vdesc, &e->menu->skel, 0, &ct, 1, ops_of(c_), &s->merged, &s->mm);
    for (int i = 0; i < s->mm.nerr; i++) {
        printf("[app] %s: refused: %s\n", e->name, s->mm.err[i]);
    }
    if (refused && !s->mm.nnodes) {
        post_status("The screens of the application have nothing that works.", true);
        delete s;
        return false;
    }
    s->vctx.desc = &s->merged;
    s->vctx.merge = &s->mm;
    s->vctx.menu_open = true;
    s->menu = new MenuCtl(&s->vctx, mh_);
    s->menu->set_app_mode(true);
    s->menu->app_start();                                // the first screen is shown: its enter operation
    s_ = s;
    sess_ = s->menu;
    sess_started_ = c_->io->now_ms();
    ui_menu_forget_rows();
    printf("[app] %s: screens up (%d nodes)\n", e->name, s->mm.nnodes);
    return true;
}

void AppHost::drop_session()
{
    if (!s_) {
        return;
    }
    delete s_->menu;
    delete s_;
    s_ = 0;
    sess_ = 0;
    ui_menu_forget_rows();
}

// ---- running and ending ---------------------------------------------------------------------------------------------------------------------

bool AppHost::service(bool menu_open)
{
    bool changed = dlg_.service();
    if (req_pending_ && menu_open) {
        open_now();
        changed = true;
    }
    if (task_done_) {
        task_done_ = false;
        task_alive_ = false;
        if (sess_ && resident_) {
            drop_session();                                  // a resident application that ended on its own (the monitor's /quit): its screens go with it
        }
        if (!sess_ && handle_ >= 0) {
            end_application();
        } else if (reply_[0]) {
            post_status(reply_, reply_bad_);                 // an application with screens says what it has to say on its own status row
            if (sess_) {
                sess_->say(reply_, reply_bad_);
            }
        }
        changed = true;
    }
    uint32_t now = c_->io->now_ms();
    if ((sess_ || dlg_.active()) && now - last_draw_ >= 200) {
        last_draw_ = now;
        changed = true;
    }
    if (sess_) {
        changed |= sess_->follow_tick();                 // the first tab's `follow`: the application asks for another tab
        if (sess_->take_close_request()) {
            char why[160];
            if (!close_session(why, sizeof(why))) {
                core_menu_->say(why, true);
            }
            changed = true;
        }
    }
    return changed;
}

void AppHost::end_application()
{
    if (reply_[0]) {
        post_status(reply_, reply_bad_);
    }
    reply_ready_ = false;
    dlg_.abandon();
    stop_jobs();
    if (end_hook_) {
        end_hook_();
    }
    if (!resident_ && handle_ >= 0) {
        lock_->lock();
        ld_->release(handle_, 0, 0);
        lock_->unlock();
    }
    ld_->set_current(-1);
    handle_ = -1;
    name_[0] = 0;
}

void AppHost::stop_jobs()
{
    for (int i = 0; i < APPJOB_MAX; i++) {
        if (jobs_[i].used && !jobs_[i].done) {
            jobs_[i].cancel = true;
        }
    }
    uint32_t t0 = c_->io->now_ms();
    for (;;) {
        bool any = false;
        for (int i = 0; i < APPJOB_MAX; i++) {
            any |= jobs_[i].used && !jobs_[i].done;
        }
        if (!any || c_->io->now_ms() - t0 > 5000) {
            break;
        }
        os_->sleep_ms(10);
    }
    for (int i = 0; i < APPJOB_MAX; i++) {
        if (jobs_[i].done) {
            jobs_[i].used = false;
        }
    }
}

bool AppHost::close_session(char *why, int whylen)
{
    if (!sess_) {
        return true;
    }
    if (!resident_) {
        stop_ = true;
        dlg_.abandon();                                  // a question the application waits on would keep it from ending
        for (int i = 0; i < APPJOB_MAX; i++) {
            if (jobs_[i].used && !jobs_[i].done) {
                jobs_[i].cancel = true;
            }
        }
        uint32_t t0 = c_->io->now_ms();
        for (;;) {
            bool any = task_alive_ && !task_done_;
            for (int i = 0; i < APPJOB_MAX; i++) {
                any |= jobs_[i].used && !jobs_[i].done;
            }
            if (!any) {
                break;
            }
            if (c_->io->now_ms() - t0 > 4000) {
                stop_ = false;
                snprintf(why, whylen, "%s is still working and cannot stop yet.", name_);
                return false;
            }
            os_->sleep_ms(10);
            dlg_.abandon();
        }
        task_alive_ = false;
        task_done_ = false;
    }
    drop_session();
    if (!resident_) {
        end_application();
        stop_ = false;
    }
    return true;
}

// ---- the service groups ---------------------------------------------------------------------------------------------------------------------

SvcJobH *AppHost::job_start(int (*fn)(SvcJobH *, void *), void *user, uint32_t stack_bytes)
{
    AppHost *H = g_host;
    if (!H || !fn) {
        return 0;
    }
    SvcJobH *j = 0;
    H->os_->lock(H->mu_);
    for (int i = 0; i < APPJOB_MAX && !j; i++) {         // a finished job's slot is free again for the next one
        if (!H->jobs_[i].used || H->jobs_[i].done) {
            j = &H->jobs_[i];
            void *sem = j->finished;
            memset(j, 0, sizeof(*j));
            j->finished = sem;
            j->used = true;
        }
    }
    H->os_->unlock(H->mu_);
    if (!j) {
        return 0;
    }
    j->fn = fn;
    j->user = user;
    uint32_t words = stack_bytes / 4 < 3072 ? 3072 : stack_bytes / 4;
    if (!H->os_->task_new("App job", job_entry, j, 0, words)) {
        j->used = false;
        return 0;
    }
    return j;
}

void AppHost::job_entry(void *p)
{
    SvcJobH *j = static_cast<SvcJobH *>(p);
    j->rc = j->fn(j, j->user);
    j->done = true;
    if (g_host) {
        g_host->os_->give(j->finished);
    }
}

bool AppHost::job_cancelled(SvcJobH *j) { return (j && j->cancel) || (g_host && g_host->stop_); }
bool AppHost::job_running(SvcJobH *j) { return j && j->used && !j->done; }
bool AppHost::job_stop_requested(void) { return g_host && g_host->stop_; }

int AppHost::job_join(SvcJobH *j)
{
    if (!j || !j->used) {
        return -1;
    }
    while (!j->done) {
        g_host->os_->take(j->finished, 20);
    }
    int rc = j->rc;
    j->used = false;
    return rc;
}

void AppHost::dlg_message(const char *title, const char *text) { if (g_host) g_host->dlg_.message(title, text); }
int AppHost::dlg_buttons(const char *title, const char *text, int b) { return g_host ? g_host->dlg_.buttons(title, text, b) : 0; }
bool AppHost::dlg_confirm(const char *title, const char *text) { return g_host && g_host->dlg_.confirm(title, text); }
int AppHost::dlg_choose(const char *title, const char *const *items, int n, int sel) { return g_host ? g_host->dlg_.choose(title, items, n, sel, 0) : -1; }
int AppHost::dlg_choose_keys(const char *title, const char *const *items, int n, const char *keys) { return g_host ? g_host->dlg_.choose(title, items, n, 0, keys) : -1; }
bool AppHost::dlg_text(const char *title, char *buf, int max) { return g_host && g_host->dlg_.text(title, buf, max); }
bool AppHost::dlg_pick_file(const char *start, const char *ext, char *out, int outlen) { return g_host && g_host->dlg_.pick_file(start, ext, out, outlen); }
bool AppHost::dlg_pick_folder(const char *start, char *out, int outlen) { return g_host && g_host->dlg_.pick_folder(start, out, outlen); }
void *AppHost::dlg_progress_open(const char *title, int total) { return g_host ? g_host->dlg_.progress_open(title, total) : 0; }
void AppHost::dlg_progress_set(void *h, int value, const char *text) { if (g_host) g_host->dlg_.progress_set(h, value, text); }
void AppHost::dlg_progress_close(void *h) { if (g_host) g_host->dlg_.progress_close(h); }

// ---- the media changed ---------------------------------------------------------------------------------------------------------------------

bool AppHost::rescan_if_changed(LibLog *log)
{
    if (handle_ >= 0 || req_pending_) {
        return false;                                   // not while an application is open: its entry is in use
    }
    static char folders[APP_FOLDERS_MAX][APP_PATH_MAX];
    int nf = AppIndex::folders(c_, folders, APP_FOLDERS_MAX);
    if (AppIndex::signature(c_->fs, folders, nf) == idx_->signature()) {
        return false;
    }
    idx_->scan(c_->fs, folders, nf, log);
    return true;
}
