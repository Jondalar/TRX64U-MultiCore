// The base's side of the core hooks (core_hooks.h, export "corehooks", major 1; T239.3 base 3.3): finds the table a library of the core exports and calls its members
// at the places the header names. A member the table lacks is a call the base makes itself (run.cc): the ring's start, the monitor's release and the ring's unfreeze. A core with no
// such export has no hooks at all.
#ifndef COREAPP_HOOKHOST_H
#define COREAPP_HOOKHOST_H

#include "ctx.h"
#include "svctab.h"

#define COREHOOKS_NAME "corehooks"
#define COREHOOKS_MAJOR 1

// Looks the table up through api->lib->find (SvcLib), once the libraries are loaded. Null `lib`, no export of that name, or an export of another major: no hooks.
// Forgets the previous state, the call is the start of a run (and of every test).
void hooks_attach(const SvcLib *lib);
bool hooks_present(void);

// `start` (core_hooks.h): called once, after the core started and before any poll. 0: done, or no hook. A reason from the hook is cut into msg (msglen - 1 characters) and 1 is
// returned; the caller tells the person. Arms base_setting_changed, which was not called until now, and calls it once for each base-owned setting the core has, in the order of
// svc_settings.h (baseset.h).
// *ran (when given) tells whether the table has a `start` member and it was called; false: the caller's built-in start is due (the ring's engine, trx_ring_engine_init).
int hooks_start(CoreCtx *c, char *msg, int msglen, bool *ran = 0);

// `menu_closed`: whether the table has the member. When it has, hooks_menu_closed() calls it and the caller makes no built-in call (the monitor's release, the ring's unfreeze); when it
// has not, those are the caller's, as with no table at all.
bool hooks_has_menu_closed(void);
bool hooks_menu_closed(void);

// The base applied the setting `key`. A hook called only after hooks_start: the start applies every setting before it, and those are not told.
void hooks_base_changed(const char *key);

#endif
