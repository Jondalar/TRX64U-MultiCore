// The base's side of the core hooks (core_hooks.h, export "corehooks", major 1; T239.3 base 3.3): finds the table a library of the core exports and calls its members
// at the places the header names. A member the table lacks is a call the base makes itself (run.cc): the ring's start, the monitor's release and the ring's unfreeze. A core with no
// such export has no hooks at all.
#ifndef COREAPP_HOOKHOST_H
#define COREAPP_HOOKHOST_H

#include "ctx.h"
#include "desc.h"
#include "svctab.h"

#define COREHOOKS_NAME "corehooks"
#define COREHOOKS_MAJOR 1

// Looks the table up through api->lib->find (SvcLib), once the libraries are loaded. Null `lib`, no export of that name, or an export of another major: no hooks.
// Forgets the previous state, the call is the start of a run (and of every test). No scope: the calls are made on the account of whoever calls.
void hooks_attach(const SvcLib *lib);
bool hooks_present(void);

// The table of the library the core's record names (DescSections::ident.hooks, CoreDesc::hooks_lib), exact major COREHOOKS_MAJOR: the table the library lists as the export "corehooks"
// in its record. Every call into it is made through the scope the caller gives, which takes the library's account for the call (LibLoader::Scope, hooklib.h): `enter` fills
// `token` (HOOK_TOKEN_BYTES, 8-aligned), `leave` closes it. Forgets the previous state like hooks_attach. 0: attached. -1: no table, and `why` says which (the library is not loaded,
// it has no export "corehooks" at major 1); the core then has no hooks.
#define HOOK_TOKEN_BYTES 32
typedef void (*HookEnter)(void *user, void *token);
typedef void (*HookLeave)(void *user, void *token);
int hooks_attach_table(const void *table, uint32_t size, HookEnter enter, HookLeave leave, void *user);
void hooks_detach(void);

// The descriptor of a core whose record names a hooks library (CoreDesc::hooks_lib), after the library is loaded and attached: the control hooks init and release are always the library's
// (a member the table lacks is a call that does nothing; a library that is not there leaves the machine held, and the answer says so); warm_reset, cold_start, media_flush and
// media_pending are the library's when its table has the member, else null in the descriptor (no hook), each member with the meaning and the place of CoreDesc's (boot.cc, run.cc,
// engines.cc). The core's [mount] list (CoreDesc::mount_slots, names only until now) gets the three functions of slot_spec, slot_mount and slot_eject, through adapters: the base's
// own buffer (Settings::MOUNT_VAL_LEN bytes) for the spec, the length 0 an empty slot, -1 a spec that does not fit (logged, the slot reads as empty). A table without all three
// takes the slots away and is the answer. Null: all done. A descriptor with no hooks_lib is not touched.
const char *hooks_bind(CoreDesc *d);

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
