// What coreapp/src/apphost.cc reaches beyond the loader, for tests/loader/test_libload.cc: it links the real AppHost to look at its job table (S14), and never opens a session,
// a dialog or a menu, so the rest of the application is answered with nothing.
#include "apphost.h"
#include "ops_glue.h"
#include "ui.h"
#include "contrib.h"
#include "filemgr.h"

bool ui_available(void) { return false; }
void ops_ask_list(CoreCtx *, const char *, int, const char *, const char *) { }   // T262 P2: ctx.ask_list (libsvc.cc) raises the menu's choice list; no menu here
void ui_menu_forget_rows(void) { }
OpRegistry *ops_of(CoreCtx *) { return 0; }
int contrib_merge(const CoreDesc *, const Skeleton *, const Overrides *, const Contributor *, int, const OpRegistry *, CoreDesc *, MenuMerge *) { return 0; }
FileMgr::FileMgr(CoreCtx *, const Port &) { }
FileMgr::~FileMgr() { }

AppDlg::AppDlg(OsPort *os, CoreFs *fs) : os_(os), fs_(fs), pk_list_(0) { }      // ~AppDlg frees pk_list_: it has to start null, as appdlg.cc's constructor sets it
void AppDlg::message(const char *, const char *) { }
int AppDlg::buttons(const char *, const char *, int) { return 0; }
bool AppDlg::confirm(const char *, const char *) { return false; }
int AppDlg::choose(const char *, const char *const *, int, int, const char *) { return -1; }
bool AppDlg::text(const char *, char *, int) { return false; }
bool AppDlg::pick_file(const char *, const char *, char *, int) { return false; }
bool AppDlg::pick_folder(const char *, char *, int) { return false; }
void *AppDlg::progress_open(const char *, int) { return 0; }
void AppDlg::progress_set(void *, int, const char *) { }
void AppDlg::progress_close(void *) { }
void AppDlg::abandon() { }
bool AppDlg::service() { return false; }

MenuCtl::MenuCtl(CoreCtx *c, MenuHost *) : fm_(c, FileMgr::Port()) { }
MenuCtl::~MenuCtl() { }                                                         // T262: the menu frees its run-time list (picklist) when it goes
void MenuCtl::set_status(const char *, bool) { }
bool MenuCtl::follow_tick() { return false; }
void MenuCtl::app_start() { }

int AppIndex::folders(const CoreCtx *, char[][APP_PATH_MAX], int) { return 0; }
uint32_t AppIndex::signature(CoreFs *, char[][APP_PATH_MAX], int) { return 0; }
void AppIndex::scan(CoreFs *, char[][APP_PATH_MAX], int, LibLog *) { }
const AppEntry *AppIndex::find(const char *) const { return 0; }
const AppEntry *AppIndex::adhoc(CoreFs *, const char *, LibLog *) { return 0; }
bool AppIndex::prepare_open(CoreFs *, LibLoader *, const AppEntry &, char *, int) const { return true; }
const FileAction *AppIndex::prg_action(CoreFs *, const char *, uint64_t, bool) { return 0; }
void AppIndex::clear() { }
