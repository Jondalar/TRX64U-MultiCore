// The base's side of the core hooks (core_hooks.h, export "corehooks", major 1; T239.3 base 3.3): finds the table a library of the core exports and calls its members
// at the places the header names. A core with no such export has no hooks, and run.cc then makes today's built-in calls (the monitor's release, the ring) itself.
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
int hooks_start(CoreCtx *c, char *msg, int msglen);

// `menu_closed`: true when a hook was called (false: none, the built-in calls are the caller's).
bool hooks_menu_closed(void);

// The base applied the setting `key`. A hook called only after hooks_start: the start applies every setting before it, and those are not told.
void hooks_base_changed(const char *key);

#endif
