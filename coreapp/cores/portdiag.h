// The diag core's descriptor and the one thing the board has to give it: the dialogs of the application host (T175). The runner of a test waits for the
// person, so it runs on a task of its own and asks through this; tests/coreapp gives it a model.
#ifndef COREAPP_PORTDIAG_H
#define COREAPP_PORTDIAG_H

#include "desc.h"

class PdHost {
public:
    virtual ~PdHost() { }
    // Runs fn(arg) on a task of its own; false when there is none.
    virtual bool spawn(void (*fn)(void *), void *arg) = 0;
    // A question with two answers and a way out: 1 yes, 0 no, -1 the person stopped (ESC). timeout_s > 0: the question closes by itself after that many seconds
    // and answers -2 (the dialog shows the seconds left); 0: waits for ever.
    virtual int ask(const char *title, const char *text, int timeout_s) = 0;
    // T201 v2: a question with a choice of answers (items, at most 8): the index of the one picked, -1 the person stopped (ESC), -2 the time ran out (timeout_s as ask()).
    virtual int choose(const char *title, const char *text, const char *const *items, int n, int timeout_s) = 0;
    // A message that waits for OK.
    virtual void notice(const char *title, const char *text) = 0;
    // A file picker over `start` (a folder) for .toml files; false when the person cancelled.
    virtual bool pick(const char *start, char *out, int outlen) = 0;
    // "YYYYMMDD-HHMMSS" from the clock, false when it has none.
    virtual bool stamp(char out[16]) = 0;
};

void portdiag_attach(PdHost *h);
// The title and text of the question a prompt of a test asks (the runner and the render bench use the same one).
void portdiag_prompt_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn);
// The title and text of a question of an A/V test (T201): the walk's position and the question, YES / NO / ESC.
void portdiag_question_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn);
extern const CoreDesc portdiag_desc;

#endif
