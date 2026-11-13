// The application host (T104.3 D2, D6, D7): what happens between "Write to Cartridge" in a menu and the application's end. Everything here runs on the menu
// task except what is marked: the host loads the file when an entry asks for it (`app.open`), starts the application's own task, gives it the service
// tables (dialogs, jobs, the debug block, the network), shows its screens as a menu of its own while it has some, and takes it away again when the
// person closes them.
//
//   an entry asks             app.open(app, action, file) runs on the executor and only sets a request (request()); nothing is loaded there
//   the menu task picks it up service(): loads the image (the core lock held while it registers), builds the screens, starts the application's task
//   the application runs      app_main(api, ctx) on a task of its own: it sets what its screens show, starts jobs, asks dialogs; it never blocks the menu task
//   the person closes         close_session(): jobs are asked to stop and waited for, the question that is open is answered "cancelled", the image goes
//
// Residence (T104.1 D10.3): a transient application is freed with everything it still holds; a resident one (trxmon) stays loaded when its screens close and
// its task keeps running; opening it again only shows the screens.
//
// One application is open at a time: a second entry says which one has to be closed first.
#ifndef COREAPP_APPHOST_H
#define COREAPP_APPHOST_H

#include "appindex.h"
#include "appdlg.h"
#include "menuctl.h"
#include "exec.h"

#define APPJOB_MAX 4

struct SvcJobH {
    bool used;
    volatile bool done;
    volatile bool cancel;
    int rc;
    int (*fn)(SvcJobH *j, void *user);
    void *user;
    void *finished;
};

class AppHost {
public:
    // `lock`: the core lock, held while an application registers or is taken away (operations hold it while they run).
    // `core_menu`: the core's own menu, which gets the status line when an application ends; `mh`: the host the application's menus ask for information.
    AppHost(CoreCtx *c, LibLoader *ld, AppIndex *idx, OsPort *os, CoreLock *lock, MenuCtl *core_menu, MenuHost *mh);
    ~AppHost();

    // Wires c->app_open and the service groups of the loader (dlg, job).
    void install();
    // Everything the board knows that the base cannot: the groups that are the machine's (trx, net, the flash).
    void set_machine_groups(const SvcTrx *trx, const SvcNet *net, const SvcC64 *c64, int (*flash_read)(uint32_t, void *, int), void (*handover)(void));

    // What the machine has to undo when an application ends (its sockets, the debug block): called with the image still there.
    void set_end_hook(void (*fn)(void)) { end_hook_ = fn; }

    // ---- the menu task ----
    // Called on every pass of the menu loop (menu open or not). True when the screen has to be drawn again. `menu_open` says whether the menu is on
    // screen: a request is only taken up while it is.
    bool service(bool menu_open);
    // Takes a rescan of the apps folders when the media changed (a stick that came after the start); true when the index changed and the menu has to
    // be merged again (the caller does it: bundle_merge and MenuCtl::rebuild).
    bool rescan_if_changed(LibLog *log);

    bool dialog_open() const { return dlg_.open(); }
    bool dialog_active() const { return dlg_.active(); }
    bool dialog_key(int k) { return dlg_.take_key(k); }
    bool dialog_apply(const DlgIntent &in) { return dlg_.apply(in); }       // T192.3: an intent of the open dialog (the key table made it)
    const DlgView &dialog_view() { return dlg_.view(); }
    AppDlg &dialog() { return dlg_; }
    // The menu of the application's screens while they are up, else null. Keys and rendering go to it instead of the core's menu.
    MenuCtl *session() { return sess_; }
    // The person closed the screens (RUN/STOP, the left arrow at the first tab). False when the application will not let go yet (a job that cannot stop);
    // `why` then says so.
    bool close_session(char *why, int whylen);
    // Is an application loaded or running (any of it)?
    bool busy() const { return handle_ >= 0 || req_pending_; }
    const char *open_name() const { return handle_ >= 0 ? name_ : ""; }

    // ---- any task: what app.open calls
    static int open_op(CoreCtx *c, const char *app, const char *action, const char *file, char *msg, int msglen);
    int request(const char *app, const char *action, const char *file, char *msg, int msglen);

    // ---- the service groups (appsvc side)
    const SvcDlg *dlg_table() const { return &dlg_tab_; }
    const SvcJob *job_table() const { return &job_tab_; }

    // For the status line after the application's task has ended: what it left in `reply`, once.
    bool take_reply(char *out, int n, bool *bad);

private:
    CoreCtx *c_;
    LibLoader *ld_;
    AppIndex *idx_;
    OsPort *os_;
    CoreLock *lock_;
    MenuCtl *core_menu_;
    MenuHost *mh_;
    AppDlg dlg_;
    SvcDlg dlg_tab_;
    SvcJob job_tab_;
    void *mu_;

    // the request
    volatile bool req_pending_;
    char req_app_[U2A3_NAME_MAX], req_action_[32], req_file_[FS_PATH_MAX];

    // the application that is open
    int handle_;                          // the loader's slot, -1: none
    char name_[U2A3_NAME_MAX];
    char action_[32], file_[FS_PATH_MAX];
    char reply_[240];
    bool reply_bad_;
    bool reply_ready_;
    volatile bool task_alive_;
    volatile bool task_done_;
    volatile bool stop_;                  // the host wants the application's task and jobs to end
    bool resident_;
    bool resident_task_started_;          // a resident application's task is started once
    uint32_t last_draw_;

    // the application's screens
    struct Session;
    Session *s_;
    MenuCtl *sess_;

    SvcJobH jobs_[APPJOB_MAX];
    uint32_t sess_started_;
    void (*end_hook_)(void);

    void open_now();                      // the request becomes an open application
    bool build_session(const AppEntry *e);
    void drop_session();
    void stop_jobs();
    void end_application();               // the task ended and no screen is up: the image goes, the reply is kept for the status line
    void post_status(const char *s, bool bad);

    static void task_entry(void *self);
    static void job_entry(void *j);
    static SvcJobH *job_start(int (*fn)(SvcJobH *, void *), void *user, uint32_t stack_bytes);
    static bool job_cancelled(SvcJobH *j);
    static int job_join(SvcJobH *j);
    static bool job_running(SvcJobH *j);
    static bool job_stop_requested(void);
    static void dlg_message(const char *title, const char *text);
    static int dlg_buttons(const char *title, const char *text, int buttons);
    static bool dlg_confirm(const char *title, const char *text);
    static int dlg_choose(const char *title, const char *const *items, int n, int sel);
    static int dlg_choose_keys(const char *title, const char *const *items, int n, const char *keys);
    static bool dlg_text(const char *title, char *buf, int max);
    static bool dlg_pick_file(const char *start, const char *ext, char *out, int outlen);
    static bool dlg_pick_folder(const char *start, char *out, int outlen);
    static void *dlg_progress_open(const char *title, int total);
    static void dlg_progress_set(void *h, int value, const char *text);
    static void dlg_progress_close(void *h);
};

#endif
