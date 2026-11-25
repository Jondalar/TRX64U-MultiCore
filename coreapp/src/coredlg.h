// The dialogs of the application host that a core's code may ask through (T260 P7: what the diag core's runner needed, moved out of the shared run.cc's diag-named hook).
// A core's test runner waits for the person, so it runs on a task of its own and asks through this; run.cc gives every app one (CoreCtx::dialogs), tests/coreapp a model.
#ifndef COREAPP_COREDLG_H
#define COREAPP_COREDLG_H

class CoreDialogs {
public:
    virtual ~CoreDialogs() { }
    // Runs fn(arg) on a task of its own; false when there is none.
    virtual bool spawn(void (*fn)(void *), void *arg) = 0;
    // A question with two answers and a way out: 1 yes, 0 no, -1 the person stopped (ESC). timeout_s > 0: the question closes by itself after that many seconds
    // and answers -2 (the dialog shows the seconds left); 0: waits for ever.
    virtual int ask(const char *title, const char *text, int timeout_s) = 0;
    // As ask() with no timer, for a text the caller rewrites: `text` is the caller's buffer, live(ud) refills it about once a second (on the menu task) and the dialog
    // changes the text in place. The default asks it as ask() does, once.
    virtual int ask_live(const char *title, const char *text, void (*live)(void *), void *ud) { (void)live; (void)ud; return ask(title, text, 0); }
    // A question with a choice of answers (items, at most 8): the index of the one picked, -1 the person stopped (ESC), -2 the time ran out (timeout_s as ask()).
    virtual int choose(const char *title, const char *text, const char *const *items, int n, int timeout_s) = 0;
    // A message that waits for OK.
    virtual void notice(const char *title, const char *text) = 0;
    // A file picker over `start` (a folder) for .toml files; false when the person cancelled.
    virtual bool pick(const char *start, char *out, int outlen) = 0;
    // "YYYYMMDD-HHMMSS" from the clock, false when it has none.
    virtual bool stamp(char out[16]) = 0;
    // false: the joystick ports feed no menu key (only the keyboards answer a prompt); true: as always. The default does nothing.
    virtual void joystick_menu(bool on) { (void)on; }
};

#endif
