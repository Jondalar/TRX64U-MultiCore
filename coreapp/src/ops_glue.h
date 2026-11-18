// The descriptor's way into the service layer (T104.1 D14): the menu, the boot code and the run loop call operations by the names the
// descriptor holds. Each call goes to the executor when there is one (run.cc starts it), else it runs on the caller - the tests, and the
// start sequence before the executor exists.
#ifndef COREAPP_OPS_GLUE_H
#define COREAPP_OPS_GLUE_H

#include "ctx.h"

// The registry of this core application: the base's operations (the shared resets, the Picture item), then the descriptor's contributor.
// Made on first use. Returns null when a registration was refused; ops_error() says which.
OpRegistry *ops_of(CoreCtx *c);
const char *ops_error(CoreCtx *c);

// An operation by name. The arguments are checked against the registered types first; a refused call never reaches the handler.
int ops_call(CoreCtx *c, const char *name, const OpIn *in, int nin, OpResult *r);
// The same call on the calling task, not through the executor (the monitor's keys: trxmon runs on the menu task and takes no core lock).
int ops_call_direct(CoreCtx *c, const char *name, const OpIn *in, int nin, OpResult *r);

// A descriptor entry's call: the bindings fill the arguments, `file` is what $file stands for (null for a task).
// `ask` is what a B_ASK binding stands for: the address the person typed (FileAction::ask).
int ops_run_bound(CoreCtx *c, const char *op, const OpBind *bind, int nbind, const char *file, OpResult *r, int32_t ask = 0, const char *ask_text = 0);

// T228 R21: an operation asks the person something and is called again with the answer. ops_ask_text: a text prompt (secret: shown as stars); ops_ask_list: a list of choices
// whose rows are the text of the state `src` (picklist.h). The menu calls `op` with the arguments "phase" (the value given here) and "answer" (the typed text, or the chosen
// row's value); the operation may ask again. An operation that asks has the arguments ARG_INT("phase"), ARG_STRING("answer"). Nothing happens when the person cancels.
void ops_ask_text(CoreCtx *c, const char *op, int phase, const char *prompt, int maxlen, bool secret);
void ops_ask_list(CoreCtx *c, const char *op, int phase, const char *prompt, const char *src);

// A setting's apply: the operation with one argument "value".
int ops_set(CoreCtx *c, const char *op, int value);

// A state value as text, null when it has none.
const char *ops_state(CoreCtx *c, const char *name);

// The base's own contributor, used by ops_of() and by desc_check().
void ops_register_base(OpRegistry &r);

#endif
