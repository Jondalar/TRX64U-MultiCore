// The core application's side of the UI service (T192.1 MVP): a UicDomain over MenuCtl. The service itself is uicore/; this file is the adapter the
// core app registers - its node tree (the menu), its intent handlers (the keys, `activate`, `set`) and its configuration (the token file).
#ifndef COREAPP_UIAPP_H
#define COREAPP_UIAPP_H

#include "ctx.h"
#include "menuctl.h"
#include "svcjson.h"
#include "uic.h"
#include "appdlg.h"

// What the run loop (run.cc) lends the adapter: the loop's own key path (picture confirm, application dialog, the menu; closes it on R_CLOSE), the
// open/close of the menu, and who owns the keys. Every call is on the menu task.
struct UiAppEnv {
    void *user;
    bool (*menu_open)(void *);
    void (*open_menu)(void *);
    void (*give_key)(void *, int key);                  // a key to whoever owns the keys: the picture question, an application's dialog, else the menu (resolve + apply)
    void (*give_intent)(void *, const MenuIntent *in);   // an intent of the menu (already translated by the key table); the loop closes the menu or the session on R_CLOSE
    bool (*picture_pending)(void *);
    bool (*app_dialog)(void *);
    MenuCtl *(*active)(void *);            // the application's session if one is open, else the core's menu
    void (*leave)(void *);                 // leave the menu at once (the menu hotkey with no service): the loop's close
    // An application's dialog (appdlg.h): the key table makes the intent of a key, the dialog applies it, the view is what it shows (T192.3 D23).
    void (*dialog_key_intent)(void *, int key, DlgIntent *out);
    void (*dialog_apply)(void *, const DlgIntent *in);
    const DlgView *(*dialog_view)(void *);
    int (*picture_left_s)(void *);        // the picture question: whole seconds left
};
// The same as give_key, with the loop's own pump: the controller's clock runs inside the service on a core that has one (T192.3 leak 9)

// What the board supplies and a bench fakes: the short critical section against the network tasks, who the menu task is, a sleep for the network tasks'
// waits, and raw material for the token (timer jitter).
// The top bar and the left of the footer exactly as the screen draws them (T192.3: the snapshot says what the screen says).
struct UiShell {
    char activity[24];
    int nchips;
    char chips[8][40];
    char footer_left[40];
};
struct UiAppPlatform {
    void (*lock)(void);
    void (*unlock)(void);
    bool (*on_ui)(void);
    void (*sleep_ms)(int ms);
    int (*entropy)(uint8_t *out, int n);
    void (*shell)(MenuCtl *m, UiShell *out);      // the screen's own top bar (ui.cc); null in a bench: the snapshot leaves the bar out
};
const UiAppPlatform *uiapp_board_platform();     // uiapp_board.cc: FreeRTOS

// Registers the domain and starts the service. The snapshot buffer lives inside. Call before restsrv_start serves anyone (or soon after: REST answers
// 404 for the /v1/ui routes until then).
void uiapp_init(CoreCtx *c, const UiAppEnv *env, const UiAppPlatform *plat);
// The service of the core app; null before uiapp_init.
UicService *uiapp_service();
// The menu task, once per pass: `changed` says the screen was redrawn or a key was taken (the revision grows), then the service runs.
// Returns true when a remote intent was applied: the screen has to be drawn again.
bool uiapp_pass(bool changed);
// The screen changed outside a pass (the menu opened or closed): the revision grows.
void uiapp_changed();
// The board's own key goes through the key intent (T192.1-S7): same dispatcher as a remote `key`.
void uiapp_local_key(int key);
// The menu hotkey and the menu button are local producers too: `open` is the open intent (a core without the service opens the menu through the loop's own hook), `leave` the intent
// that leaves the menu at once (it does not ask whether the core can run without it; the loop closes the menu after it).
void uiapp_local_open();
void uiapp_local_leave();
// The v2 snapshot as the menu task renders it now (a bench).
int uiapp_render(char *out, int cap);

// D19a: a destructive operation called over REST does not run: it is asked (the screen shows the question, the caller or the screen answers). The REST
// task calls this with the operation and the request's arguments; true: the call was queued as a question and `reply` holds the JSON of the answer
// ("asked", the question id); false: the operation is not destructive, the caller runs it.
bool uiapp_op_destructive(const char *op);
bool uiapp_op_ask(const char *op, const char *query, const char *body, int body_len, char *reply, int cap, int *status);

// A bench starts the adapter again for the next core (the service is one per application): everything is forgotten. Not called on the board.
void uiapp_reset_for_test();

#endif
