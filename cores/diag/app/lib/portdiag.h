// The diag core's descriptor (T175). The runner of a test waits for the person, so it runs on a task of its own and asks through the application host's CoreDialogs (CoreCtx::dialogs).
#ifndef COREAPP_PORTDIAG_H
#define COREAPP_PORTDIAG_H

#include "desc.h"
#include "coredlg.h"
#include "panel.h"


// The title and text of the question a prompt of a test asks (the runner and the render bench use the same one).
void portdiag_prompt_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn);
// The title and text of a question of an A/V test (T201): the walk's position and the question, YES / NO / ESC.
void portdiag_question_text(const char *test, const char *text, int idx, int total, char *t, int tn, char *x, int xn);
// A register of the core's TOML A by the name a panel gives it ("block.property[.field]"), false when it has none: the PanelResolve of the app.
bool portdiag_reg_by_name(const char *name, PanelReg *out);
extern const CoreDesc portdiag_desc;

#endif
