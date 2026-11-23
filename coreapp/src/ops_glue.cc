#include "ops_glue.h"
#include "exec.h"
#include "boot.h"
#include "netops.h"
#include "picture.h"
#include "volume.h"
#include "tone.h"
#include "baseset.h"
#include "hotkeys.h"
#include "home.h"
#include "engines.h"
#include "mediamounts.h"
#include "settings_ops.h"
#include "update.h"
#include "writeback.h"
#include "pad.h"
#include "libfiles.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

// The browser enters a disk image itself (menuctl.cc intercepts the action before it gets here); the operation exists so that a descriptor's
// action can name it and desc_check can see it.
static int op_menu_browse(CoreCtx *, const OpArgs &, OpOut &) { return OPE_OK; }
static const ArgSpec browse_arg[] = { ARG_PATH("path") };

// T104.3 D6: a menu entry of an application (a file action on its type, a node in a tab) opens it by name: the application, the action it is opened with
// (its manifest's word: "write", "dump") and the file the entry was on, empty for a node. It does not wait for the application: the host (apphost.cc) loads
// it on the menu task, and what it says goes to the status line.
static int op_app_open(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    if (!c->app_open) {
        snprintf(o.msg, o.msglen, "This build has no applications.");
        return OPE_UNSUPPORTED;
    }
    return c->app_open(c, a.s(0), a.s(1), a.s(2), o.msg, o.msglen);
}
static const ArgSpec app_open_args[] = { ARG_STRING("app", 15), ARG_STRING("action", 31), ARG_STRING("file", FS_PATH_MAX - 1) };

// T228 R1: a row or a hotkey shows a screen of the menu, named "area/tab" ("F1/timewarp": a page of Home that the core's TOML declares). The menu takes the
// request (CoreCtx::want_screen) when the operation returns, or when it opens: a hotkey pressed while the core runs opens the menu on that screen.
// `unless` names a state (empty: none): while that state has a text, the screen is refused with that text (the precondition of the screen does not hold) and
// the request is not made - the row and the hotkey take this one path.
static int op_menu_screen(CoreCtx *c, const OpArgs &a, OpOut &o)
{
    const char *why = a.s(1)[0] ? ops_state(c, a.s(1)) : 0;
    if (why && why[0]) {
        snprintf(o.msg, o.msglen, "%s", why);
        return OPE_FAILED;
    }
    snprintf(c->want_screen, sizeof(c->want_screen), "%s", a.s(0));
    o.msg[0] = 0;
    return OPE_OK;
}
static const ArgSpec screen_arg[] = { ARG_STRING("screen", 31), ARG_STRING("unless", 31) };

void ops_register_base(OpRegistry &r)
{
    r.contributor("base");
    r.add_op("menu.browse", browse_arg, 1, op_menu_browse);
    r.add_op("menu.screen", screen_arg, 2, op_menu_screen);
    r.add_op("app.open", app_open_args, 3, op_app_open);
    boot_register(r);
    engines_register(r);
    mounts_register(r);
    picture_register(r);
    volume_register(r);
    tone_register(r);
    baseset_register(r);
    hk_register(r);
    home_register(r);
    settings_register(r);
    update_register(r);
    wb_register_ops(r);
    pad_register(r);
    netops_register(r);                                            // T210 D1: WiFi, Ethernet and the streams are the board's, every core has them
    libfiles_register(r);                                          // the Library's file operations belong to the F3 browser, which every core has
}

OpRegistry *ops_of(CoreCtx *c)
{
    if (!c->ops) {
        c->ops = new OpRegistry();
        ops_register_base(*c->ops);
        if (c->desc && c->desc->register_ops) {
            c->desc->register_ops(*c->ops);
        }
    }
    return c->ops;
}

const char *ops_error(CoreCtx *c)
{
    return ops_of(c)->error();
}

int ops_call(CoreCtx *c, const char *name, const OpIn *in, int nin, OpResult *r)
{
    if (c->exec) {
        return c->exec->call(name, in, nin, r);
    }
    return ops_of(c)->call(c, name, in, nin, r);
}

int ops_call_direct(CoreCtx *c, const char *name, const OpIn *in, int nin, OpResult *r)
{
    return ops_of(c)->call(c, name, in, nin, r);
}

int ops_run_bound(CoreCtx *c, const char *op, const OpBind *bind, int nbind, const char *file, OpResult *r, int32_t ask, const char *ask_text)
{
    OpIn in[OP_ARGS_MAX];
    if (nbind > OP_ARGS_MAX) {
        snprintf(r->msg, sizeof(r->msg), "%s: more bindings than arguments", op);
        r->more = false;
        r->ndata = 0;
        return r->code = OPE_ARG;
    }
    for (int i = 0; i < nbind; i++) {
        switch (bind[i].kind) {
        case BIND_FILE: in[i] = in_str(bind[i].arg, file); break;
        case BIND_INT: in[i] = in_int(bind[i].arg, bind[i].i); break;
        case BIND_ASK: in[i] = in_int(bind[i].arg, ask); break;
        case BIND_TEXT: in[i] = in_str(bind[i].arg, ask_text ? ask_text : ""); break;
        default: in[i] = in_str(bind[i].arg, bind[i].s); break;
        }
    }
    return ops_call(c, op, in, nbind, r);
}

int ops_set(CoreCtx *c, const char *op, int value)
{
    OpResult r;
    OpIn in = in_int("value", value);
    return ops_call(c, op, &in, 1, &r);
}

// "set.<Key>" (spaces written as '_'): the label of the value the setting <Key> has, null when the core has no such setting. Lets a menu condition
// (show_state) depend on another setting without a state of the core's own.
static int fold(char ch)
{
    return ch == ' ' ? '_' : tolower((unsigned char)ch);
}

static const char *setting_state(CoreCtx *c, const char *key)
{
    if (!c->settings || !c->desc) {
        return 0;
    }
    for (int i = 0; i < c->desc->nsettings; i++) {
        const Setting &s = c->desc->settings[i];
        const char *a = s.key, *b = key;
        while (*a && *b && fold(*a) == fold(*b)) {
            a++;
            b++;
        }
        if (!*a && !*b) {
            return s.kind ? c->settings->text(i) : s.values[c->settings->value(i)];
        }
    }
    return 0;
}

static void ask_set(CoreCtx *c, uint8_t kind, const char *op, int phase, const char *prompt, const char *src, int maxlen)
{
    c->want_prompt.kind = kind;
    c->want_prompt.phase = phase;
    c->want_prompt.maxlen = maxlen;
    snprintf(c->want_prompt.op, sizeof(c->want_prompt.op), "%s", op);
    snprintf(c->want_prompt.prompt, sizeof(c->want_prompt.prompt), "%s", prompt);
    snprintf(c->want_prompt.src, sizeof(c->want_prompt.src), "%s", src);
}
void ops_ask_text(CoreCtx *c, const char *op, int phase, const char *prompt, int maxlen, bool secret) { ask_set(c, secret ? 2 : 1, op, phase, prompt, "", maxlen); }
void ops_ask_list(CoreCtx *c, const char *op, int phase, const char *prompt, const char *src) { ask_set(c, 3, op, phase, prompt, src, 0); }

const char *ops_state(CoreCtx *c, const char *name)
{
    if (!strncmp(name, "set.", 4)) {
        return setting_state(c, name + 4);
    }
    return ops_of(c)->state(c, name, 0);
}
